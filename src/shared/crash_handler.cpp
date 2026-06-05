#include "shared/crash_handler.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "shared/debug_log.h"

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on
#else
#include <execinfo.h>
#include <unistd.h>

#include <csignal>
#endif

namespace shared::crash_handler {
namespace {

char g_appName[64] = "app";
void* g_frames[64];

// Write to the crash log (durable) and stderr (live console). Uses only
// async-signal-safe primitives so it is callable from a signal handler.
void emit(const char* s) {
  if (!s) return;
  size_t len = std::strlen(s);
  shared::debug_log::writeRaw(s, len);
#if defined(_WIN32)
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  if (h && h != INVALID_HANDLE_VALUE) {
    DWORD wrote = 0;
    WriteFile(h, s, static_cast<DWORD>(len), &wrote, nullptr);
  }
#else
  ssize_t r = ::write(STDERR_FILENO, s, len);
  (void)r;
#endif
}

void emitHex(unsigned long long v) {
  static const char* kHex = "0123456789abcdef";
  char rev[16];
  int n = 0;
  if (v == 0) rev[n++] = '0';
  while (v && n < 16) {
    rev[n++] = kHex[v & 0xf];
    v >>= 4;
  }
  char out[19];
  int i = 0;
  out[i++] = '0';
  out[i++] = 'x';
  while (n > 0) out[i++] = rev[--n];
  out[i] = '\0';
  emit(out);
}

void logTerminate() {
  emit("\n==== std::terminate (uncaught exception) ====\n");
  emit("app: ");
  emit(g_appName);
  emit("\n");
  if (std::exception_ptr ep = std::current_exception()) {
    try {
      std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      emit("what(): ");
      emit(e.what());
      emit("\n");
    } catch (...) {
      emit("non-std exception thrown\n");
    }
  }
#if !defined(_WIN32)
  emit("backtrace:\n");
  int n = backtrace(g_frames, 64);
  int fd = shared::debug_log::fd();
  if (fd >= 0) backtrace_symbols_fd(g_frames, n, fd);
  backtrace_symbols_fd(g_frames, n, STDERR_FILENO);
#endif
  emit("==== END ====\n");
  std::abort();
}

#if defined(_WIN32)

LONG WINAPI winHandler(EXCEPTION_POINTERS* ep) {
  emit("\n==== FATAL CRASH ====\n");
  emit("app: ");
  emit(g_appName);
  emit("\n");
  unsigned code =
      (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
  emit("exception code: ");
  emitHex(code);
  emit("\n");
  if (ep && ep->ExceptionRecord) {
    emit("fault address: ");
    emitHex(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress));
    emit("\n");
  }

  HANDLE proc = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
  SymInitialize(proc, nullptr, TRUE);
  USHORT n = CaptureStackBackTrace(0, 62, g_frames, nullptr);
  emit("backtrace:\n");

  alignas(SYMBOL_INFO) char symbuf[sizeof(SYMBOL_INFO) + 256];
  SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symbuf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  for (USHORT i = 0; i < n; ++i) {
    DWORD64 addr = reinterpret_cast<DWORD64>(g_frames[i]);
    char line[512];
    DWORD64 disp = 0;
    if (SymFromAddr(proc, addr, &disp, sym)) {
      IMAGEHLP_LINE64 il{};
      il.SizeOfStruct = sizeof(il);
      DWORD ld = 0;
      if (SymGetLineFromAddr64(proc, addr, &ld, &il)) {
        std::snprintf(line, sizeof(line), "  #%u %s (%s:%lu)\n", i, sym->Name,
                      il.FileName, il.LineNumber);
      } else {
        std::snprintf(line, sizeof(line), "  #%u %s\n", i, sym->Name);
      }
    } else {
      std::snprintf(line, sizeof(line), "  #%u 0x%llx\n", i,
                    static_cast<unsigned long long>(addr));
    }
    emit(line);
  }
  emit("==== END CRASH ====\n");
  return EXCEPTION_EXECUTE_HANDLER;  // terminate after logging
}

void installPlatform() { SetUnhandledExceptionFilter(winHandler); }

#else  // POSIX

const char* signalName(int sig) {
  switch (sig) {
    case SIGSEGV:
      return "SIGSEGV (segmentation fault)";
    case SIGABRT:
      return "SIGABRT (abort)";
    case SIGBUS:
      return "SIGBUS (bus error)";
    case SIGFPE:
      return "SIGFPE (floating-point exception)";
    case SIGILL:
      return "SIGILL (illegal instruction)";
    default:
      return "unknown signal";
  }
}

void posixHandler(int sig, siginfo_t* info, void* /*ucontext*/) {
  emit("\n==== FATAL CRASH ====\n");
  emit("app: ");
  emit(g_appName);
  emit("\n");
  emit("signal: ");
  emit(signalName(sig));
  emit("\n");
  if (info) {
    emit("fault address: ");
    emitHex(reinterpret_cast<uintptr_t>(info->si_addr));
    emit("\n");
  }
  emit("backtrace:\n");
  int n = backtrace(g_frames, 64);
  int fd = shared::debug_log::fd();
  if (fd >= 0) backtrace_symbols_fd(g_frames, n, fd);
  backtrace_symbols_fd(g_frames, n, STDERR_FILENO);
  emit("==== END CRASH ====\n");

  // Restore the default disposition and re-raise so the OS still produces a
  // core dump / normal crash exit code.
  ::signal(sig, SIG_DFL);
  ::raise(sig);
}

void installPlatform() {
  // Dedicated stack so a stack-overflow SIGSEGV can still run the handler.
  static char altStack[64 * 1024];
  stack_t ss{};
  ss.ss_sp = altStack;
  ss.ss_size = sizeof(altStack);
  ss.ss_flags = 0;
  sigaltstack(&ss, nullptr);

  struct sigaction sa{};
  sa.sa_sigaction = posixHandler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) {
    sigaction(sig, &sa, nullptr);
  }
}

#endif

}  // namespace

void install(const std::string& appName) {
  std::strncpy(g_appName, appName.c_str(), sizeof(g_appName) - 1);
  g_appName[sizeof(g_appName) - 1] = '\0';
  std::set_terminate(logTerminate);
  installPlatform();
}

}  // namespace shared::crash_handler
