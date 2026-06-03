#include "server_memory_system.h"

#include "server_game.h"
#include "shared/components.h"

namespace {

struct SeasonColorBounds {
  float minX;
  float minY;
  float minZ;
  float maxX;
  float maxY;
  float maxZ;
};

SeasonColorBounds boundsForSeason(shared::SectionSeasonMap season) {
  constexpr float kMinZ = -500.0f;
  constexpr float kMaxZ = 500.0f;

  // Outer landscape (incl. Edge ring) AABB from gltf_inspect on landscape.glb:
  // X [-368, 341], Y [-292, 249]. Each season's "outer" walls push out to these
  // values (rounded with a little padding) so the extended landscape colorizes
  // along with the playable region. Inner divider coords (15, 90, -10) keep
  // their gating role so still-locked seasons stay desaturated.
  constexpr float kOuterNorth = 260.0f;   // +Y outer edge
  constexpr float kOuterSouth = -300.0f;  // -Y outer edge
  constexpr float kOuterEast = 350.0f;    // +X outer edge
  constexpr float kOuterWest = -370.0f;   // -X outer edge

  switch (season) {
    case shared::SectionSeasonMap::WINTER:
      // Stage 1: Start + Winter Unlocked. North wall reaches outer edge.
      return SeasonColorBounds{.minX = 15.0f,
                               .minY = -10.0f,
                               .minZ = kMinZ,
                               .maxX = 90.0f,
                               .maxY = kOuterNorth,
                               .maxZ = kMaxZ};

    case shared::SectionSeasonMap::FALL:
      // Stage 2: Start + Winter + Autumn Unlocked. North + east push out.
      return SeasonColorBounds{.minX = 15.0f,
                               .minY = -10.0f,
                               .minZ = kMinZ,
                               .maxX = kOuterEast,
                               .maxY = kOuterNorth,
                               .maxZ = kMaxZ};

    case shared::SectionSeasonMap::SUMMER:
      // Stage 3: + Summer. South wall also pushes out (only west still inner).
      return SeasonColorBounds{.minX = 15.0f,
                               .minY = kOuterSouth,
                               .minZ = kMinZ,
                               .maxX = kOuterEast,
                               .maxY = kOuterNorth,
                               .maxZ = kMaxZ};

    case shared::SectionSeasonMap::SPRING:
      // Stage 4: Whole map — all four walls at the outer landscape edge.
      return SeasonColorBounds{.minX = kOuterWest,
                               .minY = kOuterSouth,
                               .minZ = kMinZ,
                               .maxX = kOuterEast,
                               .maxY = kOuterNorth,
                               .maxZ = kMaxZ};
  }

  // Stage 0 (Default): Winter spawn + preview board on the mountain. Only the
  // north wall reaches the outer edge; the rest stays tight so the desaturate
  // still gates the preview area.
  return SeasonColorBounds{.minX = 40.0f,
                           .minY = 10.0f,
                           .minZ = kMinZ,
                           .maxX = 90.0f,
                           .maxY = kOuterNorth,
                           .maxZ = kMaxZ};
}
}  // namespace

void colorizeSection(ServerGame& game, shared::SectionSeasonMap season) {
  auto bounds = boundsForSeason(season);
  auto view =
      game.registry.view<shared::ColorBoundingBox, shared::PlayerInput>();
  for (auto entity : view) {
    auto& box = view.get<shared::ColorBoundingBox>(entity);
    box.minX = bounds.minX;
    box.minY = bounds.minY;
    box.minZ = bounds.minZ;
    box.maxX = bounds.maxX;
    box.maxY = bounds.maxY;
    box.maxZ = bounds.maxZ;
  }
}
