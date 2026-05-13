#pragma once

// Tracks VRAM usage by category. Allocation sites add bytes computed from
// internal format + dimensions; resizable resources clear their category
// before reallocating. Periodic dump also queries driver-reported usage via
// GL_NVX_gpu_memory_info when available (NVIDIA).
//
// Self-contained: include glad/gl.h before including this header.

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace shared::gpu_mem_profiler {

inline std::unordered_map<std::string, size_t> bytes_per_category;

inline void add(const char* category, size_t bytes) {
  bytes_per_category[category] += bytes;
}

inline void clear(const char* category) { bytes_per_category[category] = 0; }

// Per-pixel sizes use what hardware actually reserves (DEPTH24 → 4 byte slot,
// RGB16F → typically promoted to RGBA16F, DEPTH32F_STENCIL8 → 8 with padding).
inline size_t bytes_per_pixel(GLenum internal_format) {
  switch (internal_format) {
    case GL_R8:
    case GL_RED:
      return 1;
    case GL_R16F:
    case GL_RG8:
      return 2;
    case GL_RGBA8:
    case GL_SRGB8_ALPHA8:
    case GL_RG16F:
    case GL_RGB8:
    case GL_SRGB8:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
      return 4;
    case GL_RGB16F:
    case GL_RGBA16F:
    case GL_DEPTH32F_STENCIL8:
      return 8;
    case GL_RGB32F:
    case GL_RGBA32F:
      return 16;
    default:
      return 4;
  }
}

// Sum of mip chain (1 + 1/4 + 1/16 + ...) ≈ 4/3 × base.
inline size_t tex2d_bytes(GLenum internal_format, int w, int h,
                          bool mipped = false) {
  size_t base = static_cast<size_t>(w) * static_cast<size_t>(h) *
                bytes_per_pixel(internal_format);
  return mipped ? (base * 4 + 2) / 3 : base;
}

inline size_t tex3d_bytes(GLenum internal_format, int w, int h, int d) {
  return static_cast<size_t>(w) * static_cast<size_t>(h) *
         static_cast<size_t>(d) * bytes_per_pixel(internal_format);
}

inline size_t cubemap_bytes(GLenum internal_format, int w, int h,
                            bool mipped = false) {
  return 6 * tex2d_bytes(internal_format, w, h, mipped);
}

// NVX_gpu_memory_info enums (NVIDIA). Use raw hex so we don't depend on glad
// exposing them.
inline constexpr GLenum kNvxTotalAvailable = 0x9048;
inline constexpr GLenum kNvxCurrentAvailable = 0x9049;

inline void print() {
  std::vector<std::pair<std::string, size_t>> sorted(
      bytes_per_category.begin(), bytes_per_category.end());
  std::ranges::sort(sorted, [](const auto& a, const auto& b) {
    return a.second > b.second;
  });

  size_t tracked_total = 0;
  for (const auto& [_, v] : sorted) tracked_total += v;

  const double mb = 1024.0 * 1024.0;
  std::cout << "\n=== [ Client ] GPU Memory ===\n";

  GLint total_kb = 0, free_kb = 0;
  while (glGetError() != GL_NO_ERROR) {
  }
  glGetIntegerv(kNvxTotalAvailable, &total_kb);
  glGetIntegerv(kNvxCurrentAvailable, &free_kb);
  if (glGetError() == GL_NO_ERROR && total_kb > 0) {
    double total_mb = total_kb / 1024.0;
    double used_mb = (total_kb - free_kb) / 1024.0;
    std::cout << " Driver: " << used_mb << " MB used / " << total_mb
              << " MB total\n";
  }

  std::cout << " Tracked total: " << (tracked_total / mb) << " MB\n";
  for (const auto& [name, v] : sorted) {
    if (v == 0) continue;
    std::cout << "  - " << name << ": " << (v / mb) << " MB\n";
  }
  std::cout << "============================\n";
}

inline void end_frame() {
  static int frame_count = 0;
  if (++frame_count % 60 != 0) return;
  print();
}

}  // namespace shared::gpu_mem_profiler

#ifdef ENABLE_PROFILING
#define GPU_MEM_ADD(category, bytes) \
  shared::gpu_mem_profiler::add(category, bytes)
#define GPU_MEM_CLEAR(category) shared::gpu_mem_profiler::clear(category)
#define GPU_MEM_TEX2D(category, internal_format, w, h)        \
  shared::gpu_mem_profiler::add(                              \
      category,                                               \
      shared::gpu_mem_profiler::tex2d_bytes(internal_format, w, h, false))
#define GPU_MEM_TEX2D_MIPPED(category, internal_format, w, h) \
  shared::gpu_mem_profiler::add(                              \
      category,                                               \
      shared::gpu_mem_profiler::tex2d_bytes(internal_format, w, h, true))
#define GPU_MEM_TEX3D(category, internal_format, w, h, d) \
  shared::gpu_mem_profiler::add(                          \
      category,                                           \
      shared::gpu_mem_profiler::tex3d_bytes(internal_format, w, h, d))
#define GPU_MEM_CUBEMAP(category, internal_format, w, h) \
  shared::gpu_mem_profiler::add(                         \
      category,                                          \
      shared::gpu_mem_profiler::cubemap_bytes(internal_format, w, h, false))
#define GPU_MEM_RENDERBUFFER(category, internal_format, w, h) \
  shared::gpu_mem_profiler::add(                              \
      category,                                               \
      shared::gpu_mem_profiler::tex2d_bytes(internal_format, w, h, false))
#define GPU_MEM_FRAME_END() shared::gpu_mem_profiler::end_frame()
#else
#define GPU_MEM_ADD(category, bytes)
#define GPU_MEM_CLEAR(category)
#define GPU_MEM_TEX2D(category, internal_format, w, h)
#define GPU_MEM_TEX2D_MIPPED(category, internal_format, w, h)
#define GPU_MEM_TEX3D(category, internal_format, w, h, d)
#define GPU_MEM_CUBEMAP(category, internal_format, w, h)
#define GPU_MEM_RENDERBUFFER(category, internal_format, w, h)
#define GPU_MEM_FRAME_END()
#endif
