#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace shared::profiler {

// Terminal writes block: std::cout to a TTY stalls the calling thread on the
// kernel tty buffer (flow control) for 100s of ms when the terminal can't keep
// up. The server/client loops are single-threaded, so printing the stats block
// inline would freeze physics, networking, and rendering for that whole stall.
// Hand the already-formatted report to this background thread instead so the
// hot loop only pays a cheap string move under a mutex.
class AsyncLogger {
 public:
  static AsyncLogger& instance() {
    static AsyncLogger logger;
    return logger;
  }

  void log(std::string msg) {
    {
      std::scoped_lock lock(mutex_);
      // Bound the backlog: if the console drains slower than we produce stats
      // (the Windows console easily does), drop the oldest reports rather than
      // grow unbounded. Stale profiling output has no value anyway.
      constexpr size_t kMaxQueued = 32;
      if (queue_.size() >= kMaxQueued) queue_.pop_front();
      queue_.push_back(std::move(msg));
    }
    cv_.notify_one();
  }

 private:
  AsyncLogger() {
    std::thread([this] {
      for (;;) {
        std::string msg;
        {
          std::unique_lock lock(mutex_);
          cv_.wait(lock, [this] { return !queue_.empty(); });
          msg = std::move(queue_.front());
          queue_.pop_front();
        }
        std::cout << msg << std::flush;
      }
    }).detach();
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::string> queue_;
};

// frame_stats is touched by both the render thread (Graphics::render scopes)
// and the network thread (ClientNetwork::poll, deserializeComponents). A
// concurrent operator[] + clear() walks freed bucket nodes and SIGSEGVs.
struct ScopeStat {
  double sum = 0.0;   // total ms over the reporting window (for the average)
  double max = 0.0;   // worst single-scope ms in the window (catches stalls)
};
inline std::mutex frame_stats_mutex;
inline std::unordered_map<std::string, ScopeStat> frame_stats;
inline std::chrono::time_point<std::chrono::high_resolution_clock> frame_start;

class ScopeTimer {
 public:
  ScopeTimer(const char* name) : name_(name) {
    start_ = std::chrono::high_resolution_clock::now();
  }
  ~ScopeTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start_;
    std::scoped_lock lock(frame_stats_mutex);
    auto& stat = frame_stats[name_];
    stat.sum += elapsed.count();
    if (elapsed.count() > stat.max) stat.max = elapsed.count();
  }

 private:
  const char* name_;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

inline void start_frame() {
  frame_start = std::chrono::high_resolution_clock::now();
}

inline void end_frame(const char* context = "Frame") {
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end - frame_start;

  // Add the total time over the 60 frames.
  static double cumulative_frame_time = 0.0;
  cumulative_frame_time += elapsed.count();

  static int frame_count = 0;
  frame_count++;

  // Print stats every 60 frames
  if (frame_count % 60 == 0) {
    // Snapshot under lock so the network thread can keep accumulating into
    // the map while we sort/print without it.
    std::vector<std::pair<std::string, ScopeStat>> stats;
    {
      std::scoped_lock lock(frame_stats_mutex);
      stats.assign(frame_stats.begin(), frame_stats.end());
      frame_stats.clear();
    }
    double average_frame_time = cumulative_frame_time / 60.0;

    std::ranges::sort(stats, [](const auto& a, const auto& b) {
      return a.second.sum > b.second.sum;
    });

    // Format off the hot path into a string, then hand it to the background
    // logger. The game/render loop never touches std::cout (a single console
    // write can block this thread for 100s of ms — see AsyncLogger).
    std::ostringstream out;
    out << "\n=== [ " << context
        << " ] Profiling Stats (Average per Frame over last 60 Frames) ===\n";
    out << "Avg Frame Time: " << average_frame_time << " ms\n";
    for (const auto& stat : stats) {
      double avg_stat_time = stat.second.sum / 60.0;
      double percent = average_frame_time > 0
                           ? (avg_stat_time / average_frame_time) * 100.0
                           : 0.0;
      out << " - " << stat.first << ": " << avg_stat_time << " ms (" << percent
          << "%), max " << stat.second.max << " ms\n";
    }
    out << "========================================================\n";
    AsyncLogger::instance().log(out.str());

    cumulative_frame_time = 0.0;
  }
}

}  // namespace shared::profiler

#ifdef ENABLE_PROFILING
#define SIMPLE_PROFILE_CONCAT_(a, b) a##b
#define SIMPLE_PROFILE_CONCAT(a, b) SIMPLE_PROFILE_CONCAT_(a, b)
#define SIMPLE_PROFILE_SCOPE(name) \
  shared::profiler::ScopeTimer SIMPLE_PROFILE_CONCAT(__timer_, __LINE__)(name)
#define SIMPLE_PROFILE_FRAME_START() shared::profiler::start_frame()
#define SIMPLE_PROFILE_FRAME_END(context) shared::profiler::end_frame(context)
#else
#define SIMPLE_PROFILE_SCOPE(name)
#define SIMPLE_PROFILE_FRAME_START()
#define SIMPLE_PROFILE_FRAME_END(context)
#endif
