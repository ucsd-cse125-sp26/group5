#pragma once

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
// dbghelp.h must come after windows.h
#include <dbghelp.h>
#endif

// Tiny logger. Header-only; inline globals (C++17) keep state singleton
// across translation units. Output goes to stderr and, if openFile() was
// called, also to a log file. Both sinks are unbuffered — necessary on
// Windows, where buffered console output is silently lost when the process
// dies (which is exactly the failure mode we are chasing).

namespace shared::log {

enum class Level { TRACE = 0, DEBUG, INFO, WARN, ERR, FATAL };

inline Level g_minLevel = Level::INFO;
inline FILE* g_file = nullptr;
inline std::mutex g_mutex;

inline const char* levelName(Level l) {
  switch (l) {
    case Level::TRACE:
      return "TRACE";
    case Level::DEBUG:
      return "DEBUG";
    case Level::INFO:
      return "INFO ";
    case Level::WARN:
      return "WARN ";
    case Level::ERR:
      return "ERROR";
    case Level::FATAL:
      return "FATAL";
  }
  return "?    ";
}

// Disable buffering on stdout/stderr. On Windows, when the process crashes
// (signal, SEH, abort) the C runtime buffers are not flushed, so any printf
// output produced just before the crash is lost. Calling this at startup
// ensures every log line hits the OS immediately.
inline void initUnbuffered() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
}

inline bool openFile(const char* path) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g_file) std::fclose(g_file);
  g_file = std::fopen(path, "w");
  if (g_file) std::setvbuf(g_file, nullptr, _IONBF, 0);
  return g_file != nullptr;
}

inline void setLevel(Level l) { g_minLevel = l; }

inline void writeLine(Level level, const char* file, int line, const char* fmt,
                      va_list ap) {
  if (level < g_minLevel) return;

  using clock = std::chrono::system_clock;
  auto now = clock::now();
  auto t = clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count() %
            1000;
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char ts[32];
  std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03lld", tm.tm_hour, tm.tm_min,
                tm.tm_sec, static_cast<long long>(ms));

  char body[1024];
  std::vsnprintf(body, sizeof(body), fmt, ap);

  const char* basename = file;
  for (const char* p = file; *p; ++p) {
    if (*p == '/' || *p == '\\') basename = p + 1;
  }

  std::lock_guard<std::mutex> lk(g_mutex);
  std::fprintf(stderr, "[%s %s %s:%d] %s\n", ts, levelName(level), basename,
               line, body);
  std::fflush(stderr);
  if (g_file) {
    std::fprintf(g_file, "[%s %s %s:%d] %s\n", ts, levelName(level), basename,
                 line, body);
    std::fflush(g_file);
  }
}

inline void log(Level level, const char* file, int line, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  writeLine(level, file, line, fmt, ap);
  va_end(ap);
}

// Breadcrumb: a process-global pointer to a string describing what the main
// thread is currently doing. Updated via the RAII Breadcrumb / LOG_BREADCRUMB
// macro. Read by the crash handlers to give context for *where* in the tick
// the crash happened — including crashes in Jolt worker threads, since the
// main thread's breadcrumb still says "physics.step" while the worker is
// running.
inline std::atomic<const char*> g_breadcrumb{nullptr};

class Breadcrumb {
 public:
  explicit Breadcrumb(const char* what)
      : prev_(g_breadcrumb.load(std::memory_order_relaxed)) {
    g_breadcrumb.store(what, std::memory_order_relaxed);
  }
  ~Breadcrumb() { g_breadcrumb.store(prev_, std::memory_order_relaxed); }
  Breadcrumb(const Breadcrumb&) = delete;
  Breadcrumb& operator=(const Breadcrumb&) = delete;

 private:
  const char* prev_;
};

#if defined(_WIN32)

// Symbol resolution must be initialized once. Idempotent flag.
inline std::atomic<bool> g_symInit{false};

inline void initSymbols() {
  bool expected = false;
  if (!g_symInit.compare_exchange_strong(expected, true)) return;
  HANDLE proc = GetCurrentProcess();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
  // SymInitialize on the current process; nullptr search path uses defaults
  // (the .pdb next to the .exe is found automatically).
  SymInitialize(proc, nullptr, TRUE);
}

// Logs a stack trace at the current call site. `skip` discards the top N
// frames (the trace starts inside whatever function calls this), `frames`
// caps the depth.
inline void logStackTrace(int skip = 0, int frames = 32) {
  initSymbols();
  HANDLE proc = GetCurrentProcess();

  void* stack[64];
  USHORT count = CaptureStackBackTrace(
      static_cast<DWORD>(skip), static_cast<DWORD>(frames), stack, nullptr);

  // SYMBOL_INFO has a flexible-length Name field; allocate room for it.
  alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = MAX_SYM_NAME;

  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

  for (USHORT i = 0; i < count; ++i) {
    auto addr = reinterpret_cast<DWORD64>(stack[i]);
    DWORD64 disp = 0;
    DWORD lineDisp = 0;
    const char* name = "<unknown>";
    if (SymFromAddr(proc, addr, &disp, sym)) name = sym->Name;
    if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
      log(Level::FATAL, "stack", static_cast<int>(i),
          "  #%02u %s+0x%llx (%s:%lu)", static_cast<unsigned>(i), name,
          static_cast<unsigned long long>(disp), line.FileName,
          static_cast<unsigned long>(line.LineNumber));
    } else {
      log(Level::FATAL, "stack", static_cast<int>(i),
          "  #%02u %s+0x%llx [0x%llx]", static_cast<unsigned>(i), name,
          static_cast<unsigned long long>(disp),
          static_cast<unsigned long long>(addr));
    }
  }
}

inline const char* sehExceptionName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
      return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
      return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:
      return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:
      return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:
      return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:
      return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
      return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
      return "INT_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION:
      return "PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:
      return "STACK_OVERFLOW";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
      return "NONCONTINUABLE_EXCEPTION";
    default:
      return "UNKNOWN";
  }
}

// One-shot guard so we don't recursively log if the logger itself faults
// (e.g., dbghelp dies on a corrupted stack). Once set, additional fatal
// SEH events are silently passed through.
inline std::atomic<bool> g_alreadyLoggedCrash{false};

inline bool isFatalSehCode(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
      return true;
    default:
      // Includes 0xE06D7363 (C++ throw), 0x40010006 (DbgPrint), debugger
      // breakpoints, etc. — we don't want to log on those.
      return false;
  }
}

// Common body: log the exception, breadcrumb, and stack. Used by both the
// vectored handler (preferred — runs before any frame-based SEH) and the
// unhandled-exception filter (fallback).
inline void logSehException(EXCEPTION_POINTERS* info) {
  auto* rec = info->ExceptionRecord;
  log(Level::FATAL, __FILE__, __LINE__,
      "SEH exception %s (0x%08lx) at 0x%p in thread %lu",
      sehExceptionName(rec->ExceptionCode),
      static_cast<unsigned long>(rec->ExceptionCode), rec->ExceptionAddress,
      static_cast<unsigned long>(GetCurrentThreadId()));

  if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
       rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
      rec->NumberParameters >= 2) {
    const char* op = rec->ExceptionInformation[0] == 0   ? "read"
                     : rec->ExceptionInformation[0] == 1 ? "write"
                     : rec->ExceptionInformation[0] == 8 ? "execute(DEP)"
                                                         : "?";
    log(Level::FATAL, __FILE__, __LINE__, "  fault: %s @ 0x%p", op,
        reinterpret_cast<void*>(rec->ExceptionInformation[1]));
  }

  if (auto* crumb = g_breadcrumb.load(std::memory_order_relaxed)) {
    log(Level::FATAL, __FILE__, __LINE__, "  last main-thread breadcrumb: %s",
        crumb);
  } else {
    log(Level::FATAL, __FILE__, __LINE__,
        "  last main-thread breadcrumb: <none>");
  }

  log(Level::FATAL, __FILE__, __LINE__, "stack trace:");
  // Skip the OS dispatch frames above this filter. Over-skipping is
  // harmless, the trace just starts a level deeper.
  logStackTrace(2, 32);

  // Make sure the file sink is on disk before the process dies.
  std::fflush(stderr);
  std::fflush(stdout);
  {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_file) std::fflush(g_file);
  }
}

// Vectored exception handler: runs BEFORE any frame-based SEH handler and
// is not displaceable by the C++ runtime, unlike SetUnhandledExceptionFilter.
// On MinGW (build-windows.sh's toolchain) this is the only mechanism that
// reliably fires for hardware exceptions like access violations — the
// unhandled-exception filter can be hooked by GCC's EH machinery and never
// called. Returns EXCEPTION_CONTINUE_SEARCH so normal handling proceeds and
// the process terminates as it would have anyway.
inline LONG WINAPI sehVectoredHandler(EXCEPTION_POINTERS* info) {
  if (!isFatalSehCode(info->ExceptionRecord->ExceptionCode)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  bool expected = false;
  if (g_alreadyLoggedCrash.compare_exchange_strong(expected, true)) {
    logSehException(info);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

// Unhandled-exception filter: fallback path on toolchains where vectored
// handlers don't suffice. If the vectored handler already logged this
// crash, we skip and just terminate.
inline LONG WINAPI sehFilter(EXCEPTION_POINTERS* info) {
  bool expected = false;
  if (g_alreadyLoggedCrash.compare_exchange_strong(expected, true)) {
    logSehException(info);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

#endif  // _WIN32

inline const char* signalName(int sig) {
  switch (sig) {
    case SIGSEGV:
      return "SIGSEGV";
    case SIGABRT:
      return "SIGABRT";
    case SIGFPE:
      return "SIGFPE";
    case SIGILL:
      return "SIGILL";
    case SIGINT:
      return "SIGINT";
    case SIGTERM:
      return "SIGTERM";
    default:
      return "?";
  }
}

// Signal handler: log and abort. Signal handlers are async-signal-unsafe
// territory, but this is diagnostics — we accept the risk in exchange for
// any output at all when the server otherwise dies silently.
inline void signalHandler(int sig) {
  log(Level::FATAL, __FILE__, __LINE__, "fatal signal: %s (%d)",
      signalName(sig), sig);
  if (auto* crumb = g_breadcrumb.load(std::memory_order_relaxed)) {
    log(Level::FATAL, __FILE__, __LINE__, "  last main-thread breadcrumb: %s",
        crumb);
  }
  std::fflush(stderr);
  std::fflush(stdout);
  {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_file) std::fflush(g_file);
  }
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

inline void installFatalHandlers() {
  std::signal(SIGSEGV, signalHandler);
  std::signal(SIGABRT, signalHandler);
  std::signal(SIGFPE, signalHandler);
  std::signal(SIGILL, signalHandler);
  std::signal(SIGTERM, signalHandler);

#if defined(_WIN32)
  // SEH plumbing: catches access violations etc. before the C runtime maps
  // them to SIGSEGV, gives us the actual faulting address + stack trace.
  // Two layers because they fail in different toolchains:
  //   - AddVectoredExceptionHandler: runs BEFORE any frame-based SEH and is
  //     immune to GCC's EH machinery on MinGW (build-windows.sh's toolchain).
  //     Returns CONTINUE_SEARCH after logging so termination proceeds normally.
  //   - SetUnhandledExceptionFilter: fallback for environments where the
  //     vectored handler is suppressed (some debuggers, sandboxes).
  initSymbols();
  AddVectoredExceptionHandler(/*FirstHandler=*/1, sehVectoredHandler);
  SetUnhandledExceptionFilter(sehFilter);
#endif

  std::set_terminate([]() {
    try {
      if (auto eptr = std::current_exception()) std::rethrow_exception(eptr);
      log(Level::FATAL, __FILE__, __LINE__,
          "std::terminate called with no active exception");
    } catch (const std::exception& e) {
      log(Level::FATAL, __FILE__, __LINE__, "std::terminate: %s", e.what());
    } catch (...) {
      log(Level::FATAL, __FILE__, __LINE__,
          "std::terminate: unknown exception type");
    }
    std::abort();
  });
}

}  // namespace shared::log

#define LOG_TRACE(...)                                                \
  ::shared::log::log(::shared::log::Level::TRACE, __FILE__, __LINE__, \
                     __VA_ARGS__)
#define LOG_DEBUG(...)                                                \
  ::shared::log::log(::shared::log::Level::DEBUG, __FILE__, __LINE__, \
                     __VA_ARGS__)
#define LOG_INFO(...)                                                \
  ::shared::log::log(::shared::log::Level::INFO, __FILE__, __LINE__, \
                     __VA_ARGS__)
#define LOG_WARN(...)                                                \
  ::shared::log::log(::shared::log::Level::WARN, __FILE__, __LINE__, \
                     __VA_ARGS__)
#define LOG_ERROR(...) \
  ::shared::log::log(::shared::log::Level::ERR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...)                                                \
  ::shared::log::log(::shared::log::Level::FATAL, __FILE__, __LINE__, \
                     __VA_ARGS__)

// RAII breadcrumb: declares a local Breadcrumb variable that updates the
// global breadcrumb pointer for the lifetime of the enclosing scope. Cheap
// (one atomic store on entry, one on exit). Read by the crash handler.
#define LOG_BREADCRUMB_CONCAT_INNER(a, b) a##b
#define LOG_BREADCRUMB_CONCAT(a, b) LOG_BREADCRUMB_CONCAT_INNER(a, b)
#define LOG_BREADCRUMB(name) \
  ::shared::log::Breadcrumb LOG_BREADCRUMB_CONCAT(__bc_, __LINE__)(name)
