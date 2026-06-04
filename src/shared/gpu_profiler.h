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
// Master gate: when false, no GL timer queries are issued (zero cost). A
// profiling build or the on-screen perf HUD flips this on per frame.
inline bool g_active = false;
// Smoothed per-pass ms for the HUD, updated every frame independent of the
// 60-frame stdout cadence in end_gpu_frame().
inline std::unordered_map<std::string, double> gpu_hud_ms;
inline const std::unordered_map<std::string, double>& hud_stats() {
  return gpu_hud_ms;
}

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
  GpuScopeTimer(const char* name) : name_(name), active_(g_active) {
    if (!active_) return;
    auto& p = pool_for(name);
    glQueryCounter(p.begin_q[ring_idx], GL_TIMESTAMP);
  }
  ~GpuScopeTimer() {
    if (!active_) return;
    auto& p = pool_for(name_);
    glQueryCounter(p.end_q[ring_idx], GL_TIMESTAMP);
    p.in_flight[ring_idx] = true;
  }

 private:
  const char* name_;
  bool active_;
};

inline void begin_gpu_frame() {
  if (!g_active) return;
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
    const double ms = (t1 - t0) / 1.0e6;  // ns -> ms
    gpu_frame_stats[name] += ms;
    auto [hit, fresh] = gpu_hud_ms.try_emplace(name, ms);
    if (!fresh) hit->second = hit->second * 0.9 + ms * 0.1;  // EMA
    p.in_flight[ring_idx] = false;
  }
}

inline void end_gpu_frame() {
  if (!g_active) return;
  static int frame_count = 0;
  if (++frame_count % 60 != 0) return;

  if (gpu_frame_stats.empty()) return;
#ifdef ENABLE_PROFILING
  std::vector<std::pair<std::string, double>> stats(gpu_frame_stats.begin(),
                                                    gpu_frame_stats.end());
  std::ranges::sort(
      stats, [](const auto& a, const auto& b) { return a.second > b.second; });
  std::cout << "\n=== [ Client ] GPU Timings (Average per Frame over 60 "
               "Frames) ===\n";
  for (const auto& stat : stats) {
    double avg_stat_time = stat.second / 60.0;
    std::cout << " - " << stat.first << ": " << avg_stat_time << " ms\n";
  }
  std::cout << "========================================================\n";
#endif
  gpu_frame_stats.clear();
}

}  // namespace shared::gpu_profiler

// Always compiled; runtime-gated by g_active (see above) so the on-screen perf
// HUD can enable timing in any build. When g_active is false the scope ctor
// early-outs before any GL call, so the cost is a single bool check.
// Two-level paste so __LINE__ expands before ## (avoids same-token collision
// when two scopes share a block).
#define GPU_PROFILE_CONCAT_(a, b) a##b
#define GPU_PROFILE_CONCAT(a, b) GPU_PROFILE_CONCAT_(a, b)
#define GPU_PROFILE_SCOPE(name)                                        \
  shared::gpu_profiler::GpuScopeTimer GPU_PROFILE_CONCAT(__gpu_timer_, \
                                                         __LINE__)(name)
#define GPU_PROFILE_FRAME_BEGIN() shared::gpu_profiler::begin_gpu_frame()
#define GPU_PROFILE_FRAME_END() shared::gpu_profiler::end_gpu_frame()
