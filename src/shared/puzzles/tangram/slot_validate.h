#pragma once

// 2D footprint overlap tests for tangram ghost slots (matches client
// asset.cpp).

#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "shared/puzzles/tangram/puzzle_data.h"
#include "shared/puzzles/tangram/slot_layout.h"

namespace shared::tangram_slot_validate {

namespace detail {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

inline void footprintLocal(const tangram_puzzle::PieceDef& def,
                           std::vector<Vec2>& out) {
  out.clear();
  switch (def.shape) {
    case tangram_puzzle::PieceShape::LargeTriangle:
    case tangram_puzzle::PieceShape::MediumTriangle:
    case tangram_puzzle::PieceShape::SmallTriangle:
      out = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}};
      break;
    case tangram_puzzle::PieceShape::Square:
      out = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
      break;
    case tangram_puzzle::PieceShape::Parallelogram: {
      const float w = 0.42f;
      const float h = 0.28f;
      const float sl = 0.22f;
      out = {{-w, -h}, {w, -h}, {w + sl, h}, {-w + sl, h}};
      break;
    }
  }
}

inline std::vector<Vec2> worldPolygon(const tangram_puzzle::PieceDef& def,
                                      float relX, float relY, float rotRad) {
  std::vector<Vec2> local;
  footprintLocal(def, local);
  const float c = std::cos(rotRad);
  const float s = std::sin(rotRad);
  std::vector<Vec2> world;
  world.reserve(local.size());
  for (const Vec2& p : local) {
    const float lx = p.x * def.scaleX;
    const float ly = p.y * def.scaleY;
    world.push_back({relX + c * lx - s * ly, relY + s * lx + c * ly});
  }
  return world;
}

inline bool satOverlap(const std::vector<Vec2>& a, const std::vector<Vec2>& b,
                       float epsilon = 0.015f) {
  auto project = [&](const std::vector<Vec2>& poly, float ax, float ay) {
    float mn = 1e9f;
    float mx = -1e9f;
    for (const Vec2& p : poly) {
      const float d = p.x * ax + p.y * ay;
      mn = std::min(mn, d);
      mx = std::max(mx, d);
    }
    return std::pair<float, float>{mn, mx};
  };

  auto testAxes = [&](const std::vector<Vec2>& poly) {
    const int n = static_cast<int>(poly.size());
    for (int i = 0; i < n; ++i) {
      const Vec2& p0 = poly[static_cast<size_t>(i)];
      const Vec2& p1 = poly[static_cast<size_t>((i + 1) % n)];
      const float ex = p1.x - p0.x;
      const float ey = p1.y - p0.y;
      float ax = -ey;
      float ay = ex;
      const float len = std::hypot(ax, ay);
      if (len < 1e-6f) continue;
      ax /= len;
      ay /= len;
      const auto [aMn, aMx] = project(a, ax, ay);
      const auto [bMn, bMx] = project(b, ax, ay);
      if (aMn > bMx + epsilon || bMn > aMx + epsilon) return false;
    }
    return true;
  };

  return testAxes(a) && testAxes(b);
}

inline float cyclicVertexMismatch(const std::vector<Vec2>& a,
                                  const std::vector<Vec2>& b) {
  if (a.size() != b.size() || a.empty()) return 1e9f;
  const size_t n = a.size();
  float best = 1e9f;
  auto tryOffsets = [&](bool reverse) {
    for (size_t off = 0; off < n; ++off) {
      float mx = 0.0f;
      for (size_t i = 0; i < n; ++i) {
        const size_t j = reverse ? (off + n - i) % n : (i + off) % n;
        mx = std::max(mx, std::hypot(a[i].x - b[j].x, a[i].y - b[j].y));
      }
      best = std::min(best, mx);
    }
  };
  tryOffsets(false);
  tryOffsets(true);
  return best;
}

}  // namespace detail

// True when yaw matches the slot and 2D footprints overlap (handles square
// vertex winding — index-wise vertex distance falsely fails at 45°).
[[nodiscard]] inline bool footprintsMatch(const tangram_puzzle::PieceDef& def,
                                          float pieceRelX, float pieceRelY,
                                          float pieceYawRad, float slotRelX,
                                          float slotRelY, float slotYawRad,
                                          float satEpsilon = 0.04f) {
  const float qPiece = tangram_puzzle::quantizeYawToRotateStep(pieceYawRad);
  const float qSlot = tangram_puzzle::quantizeYawToRotateStep(slotYawRad);
  if (!tangram_puzzle::yawMatchesTarget(qPiece, qSlot)) {
    return false;
  }
  const std::vector<detail::Vec2> piecePoly =
      detail::worldPolygon(def, pieceRelX, pieceRelY, qPiece);
  const std::vector<detail::Vec2> slotPoly =
      detail::worldPolygon(def, slotRelX, slotRelY, qSlot);
  return detail::satOverlap(piecePoly, slotPoly, satEpsilon);
}

[[nodiscard]] inline bool slotPosesOverlap(
    const std::array<tangram_slot::SlotPose, tangram_puzzle::kPieceCount>&
        poses) {
  std::array<std::vector<detail::Vec2>, tangram_puzzle::kPieceCount> polys;
  for (int i = 0; i < tangram_puzzle::kPieceCount; ++i) {
    const tangram_puzzle::PieceDef& def = tangram_puzzle::kPieces[i];
    const tangram_slot::SlotPose& pose = poses[static_cast<size_t>(i)];
    polys[static_cast<size_t>(i)] =
        detail::worldPolygon(def, pose.relX, pose.relY, pose.rotRad);
  }
  for (int i = 0; i < tangram_puzzle::kPieceCount; ++i) {
    for (int j = i + 1; j < tangram_puzzle::kPieceCount; ++j) {
      if (detail::satOverlap(polys[static_cast<size_t>(i)],
                             polys[static_cast<size_t>(j)])) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] inline bool defaultCodeLayoutOverlaps() {
  std::array<tangram_slot::SlotPose, tangram_puzzle::kPieceCount> poses{};
  for (int i = 0; i < tangram_puzzle::kPieceCount; ++i) {
    const tangram_puzzle::PieceDef& def = tangram_puzzle::kPieces[i];
    poses[static_cast<size_t>(i)].relX = def.targetRelX;
    poses[static_cast<size_t>(i)].relY = def.targetRelY;
    poses[static_cast<size_t>(i)].rotRad = def.targetRotRad;
    poses[static_cast<size_t>(i)].valid = true;
  }
  return slotPosesOverlap(poses);
}

[[nodiscard]] inline bool mapSlotLayoutUsable(
    const tangram_slot::Config& layout) {
  if (!layout.anyFromMap) return false;
  std::array<tangram_slot::SlotPose, tangram_puzzle::kPieceCount> poses{};
  for (int i = 0; i < tangram_puzzle::kPieceCount; ++i) {
    poses[static_cast<size_t>(i)] = layout.slots[i];
    if (!poses[static_cast<size_t>(i)].valid) return false;
  }
  return !slotPosesOverlap(poses);
}

}  // namespace shared::tangram_slot_validate
