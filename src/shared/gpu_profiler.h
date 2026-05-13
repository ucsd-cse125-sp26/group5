#pragma once

// GPU-side companion to simple_profiler.h. Uses glQueryCounter(GL_TIMESTAMP)
// pairs so scopes can nest freely (GL_TIME_ELAPSED queries cannot nest).
// Results are read back kFrameLatency frames later to avoid pipeline stalls.
//
// Self-contained: this header is independent of simple_profiler.h. Include
// glad/gl.h before including this header.

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace shared::gpu_profiler {

inline constexpr int kFrameLatency = 3;

struct ScopePool {
  GLuint begin_q[kFrameLatency]{};
  GLuint end_q[kFrameLatency]{};
  bool in_flight[kFrameLatency]{};
};

inline std::unordered_map<std::string, ScopePool> pools;
inline std::unordered_map<std::string, double> gpu_frame_stats;
inline int ring_idx = 0;

inline ScopePool& pool_for(const char* name) {
  auto [it, inserted] = pools.try_emplace(name);
  if (inserted) {
    glGenQueries(kFrameLatency, it->second.begin_q);
    glGenQueries(kFrameLatency, it->second.end_q);
  }
  return it->second;
}

class GpuScopeTimer {
 public:
  GpuScopeTimer(const char* name) : name_(name) {
    auto& p = pool_for(name);
    glQueryCounter(p.begin_q[ring_idx], GL_TIMESTAMP);
  }
  ~GpuScopeTimer() {
    auto& p = pool_for(name_);
    glQueryCounter(p.end_q[ring_idx], GL_TIMESTAMP);
    p.in_flight[ring_idx] = true;
  }

 private:
  const char* name_;
};

inline void begin_gpu_frame() {
  ring_idx = (ring_idx + 1) % kFrameLatency;
  // Drain results from the slot we're about to overwrite (kFrameLatency-1
  // frames old). Skip silently if not yet available — better to under-sample
  // than to stall.
  for (auto& [name, p] : pools) {
    if (!p.in_flight[ring_idx]) continue;
    GLint avail = GL_FALSE;
    glGetQueryObjectiv(p.end_q[ring_idx], GL_QUERY_RESULT_AVAILABLE, &avail);
    if (!avail) continue;
    GLuint64 t0 = 0, t1 = 0;
    glGetQueryObjectui64v(p.begin_q[ring_idx], GL_QUERY_RESULT, &t0);
    glGetQueryObjectui64v(p.end_q[ring_idx], GL_QUERY_RESULT, &t1);
    gpu_frame_stats[name] += (t1 - t0) / 1.0e6;  // ns -> ms
    p.in_flight[ring_idx] = false;
  }
}

inline void end_gpu_frame() {
  static int frame_count = 0;
  if (++frame_count % 60 != 0) return;

  if (gpu_frame_stats.empty()) return;
  std::vector<std::pair<std::string, double>> stats(gpu_frame_stats.begin(),
                                                    gpu_frame_stats.end());
  std::ranges::sort(stats, [](const auto& a, const auto& b) {
    return a.second > b.second;
  });
  std::cout
      << "\n=== [ Client ] GPU Timings (Average per Frame over 60 Frames) ===\n";
  for (const auto& stat : stats) {
    double avg_stat_time = stat.second / 60.0;
    std::cout << " - " << stat.first << ": " << avg_stat_time << " ms\n";
  }
  std::cout << "========================================================\n";
  gpu_frame_stats.clear();
}

}  // namespace shared::gpu_profiler

#ifdef ENABLE_PROFILING
// Two-level paste so __LINE__ expands before ## (avoids same-token collision
// when two scopes share a block).
#define GPU_PROFILE_CONCAT_(a, b) a##b
#define GPU_PROFILE_CONCAT(a, b) GPU_PROFILE_CONCAT_(a, b)
#define GPU_PROFILE_SCOPE(name)                              \
  shared::gpu_profiler::GpuScopeTimer GPU_PROFILE_CONCAT(    \
      __gpu_timer_, __LINE__)(name)
#define GPU_PROFILE_FRAME_BEGIN() shared::gpu_profiler::begin_gpu_frame()
#define GPU_PROFILE_FRAME_END() shared::gpu_profiler::end_gpu_frame()
#else
#define GPU_PROFILE_SCOPE(name)
#define GPU_PROFILE_FRAME_BEGIN()
#define GPU_PROFILE_FRAME_END()
#endif
