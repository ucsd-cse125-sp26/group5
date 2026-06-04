#pragma once

// Per-frame main-pass draw / cull counters surfaced by the on-screen perf HUD.
// Always compiled; a couple of int increments per entity, negligible cost. The
// renderer resets these each frame and counts only the camera (non-shadow)
// pass, so "culled" directly reflects the main-pass frustum-culling win.
namespace shared::draw_stats {

inline int entitiesDrawn = 0;
inline int entitiesCulled = 0;

inline void reset() {
  entitiesDrawn = 0;
  entitiesCulled = 0;
}

}  // namespace shared::draw_stats
