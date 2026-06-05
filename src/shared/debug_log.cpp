#include "shared/debug_log.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace shared::debug_log {
namespace {

std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<std::string> g_queue;
std::thread g_worker;
std::atomic<bool> g_running{false};
std::atomic<int> g_fd{-1};

int openFd(const std::string& path) {
#if defined(_WIN32)
  return ::_open(path.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY,
                 _S_IREAD | _S_IWRITE);
#else
  return ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
}

void rawWrite(int fd, const char* p, size_t n) {
  while (n > 0) {
#if defined(_WIN32)
    int w = ::_write(fd, p, static_cast<unsigned>(n));
#else
    ssize_t w = ::write(fd, p, n);
#endif
    if (w <= 0) break;
    p += w;
    n -= static_cast<size_t>(w);
  }
}

void formatNow(char* buf, size_t n) {
  using namespace std::chrono;
  auto now = system_clock::now();
  std::time_t t = system_clock::to_time_t(now);
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm tmv{};
#if defined(_WIN32)
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char base[20];
  std::strftime(base, sizeof(base), "%H:%M:%S", &tmv);
  std::snprintf(buf, n, "%s.%03d", base, static_cast<int>(ms));
}

void workerMain() {
  std::deque<std::string> local;
  for (;;) {
    {
      std::unique_lock<std::mutex> lk(g_mutex);
      g_cv.wait(lk, [] {
        return !g_queue.empty() || !g_running.load(std::memory_order_acquire);
      });
      if (g_queue.empty() && !g_running.load(std::memory_order_acquire)) break;
      local.swap(g_queue);
    }
    int fd = g_fd.load(std::memory_order_acquire);
    for (auto& s : local) rawWrite(fd, s.data(), s.size());
    local.clear();
  }
}

}  // namespace

bool init(const std::string& path) {
  if (g_running.load(std::memory_order_acquire)) return true;
  int fd = openFd(path);
  if (fd < 0) {
    std::fprintf(stderr, "debug_log: could not open %s\n", path.c_str());
    return false;
  }
  g_fd.store(fd, std::memory_order_release);
  g_running.store(true, std::memory_order_release);
  g_worker = std::thread(workerMain);

  char ts[32];
  formatNow(ts, sizeof(ts));
  char banner[96];
  int m = std::snprintf(banner, sizeof(banner),
                        "\n===== SESSION START %s =====\n", ts);
  if (m > 0) rawWrite(fd, banner, static_cast<size_t>(m));
  return true;
}

void shutdown() {
  bool was = g_running.exchange(false, std::memory_order_acq_rel);
  g_cv.notify_one();
  if (g_worker.joinable()) g_worker.join();
  int fd = g_fd.exchange(-1, std::memory_order_acq_rel);
  if (was && fd >= 0) {
    const char foot[] = "===== SESSION END =====\n";
    rawWrite(fd, foot, sizeof(foot) - 1);
  }
  if (fd >= 0) {
#if defined(_WIN32)
    ::_close(fd);
#else
    ::close(fd);
#endif
  }
}

void logf(const char* fmt, ...) {
  if (!g_running.load(std::memory_order_acquire)) return;

  char body[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = std::vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  if (n < 0) return;

  char ts[32];
  formatNow(ts, sizeof(ts));

  std::string line;
  line.reserve(40 + std::strlen(body));
  line.push_back('[');
  line.append(ts);
  line.append("] ");
  line.append(body);
  if (line.empty() || line.back() != '\n') line.push_back('\n');

  {
    std::scoped_lock lk(g_mutex);
    g_queue.push_back(std::move(line));
  }
  g_cv.notify_one();
}

void writeRaw(const char* data, size_t len) {
  int fd = g_fd.load(std::memory_order_acquire);
  if (fd >= 0) rawWrite(fd, data, len);
}

int fd() { return g_fd.load(std::memory_order_acquire); }

}  // namespace shared::debug_log
