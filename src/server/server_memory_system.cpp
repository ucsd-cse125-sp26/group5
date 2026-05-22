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

  switch (season) {
    case shared::SectionSeasonMap::WINTER:
      // Stage 1: Start + Winter Unlocked
      return SeasonColorBounds{.minX = 40.0f,
                               .minY = 25.0f,
                               .minZ = kMinZ,
                               .maxX = 90.0f,
                               .maxY = 105.0f,
                               .maxZ = kMaxZ};

    case shared::SectionSeasonMap::FALL:
      // Stage 2: Start + Winter + Autumn Unlocked
      return SeasonColorBounds{.minX = 40.0f,
                               .minY = 25.0f,
                               .minZ = kMinZ,
                               .maxX = 170.0f,
                               .maxY = 105.0f,
                               .maxZ = kMaxZ};

    case shared::SectionSeasonMap::SUMMER:
      // Stage 3: Start + Winter + Autumn + Summer Unlocked
      return SeasonColorBounds{.minX = 40.0f,
                               .minY = -105.0f,
                               .minZ = kMinZ,
                               .maxX = 170.0f,
                               .maxY = 105.0f,
                               .maxZ = kMaxZ};

    case shared::SectionSeasonMap::SPRING:
      // Stage 4: The Whole Map Unlocked
      return SeasonColorBounds{.minX = -170.0f,
                               .minY = -105.0f,
                               .minZ = kMinZ,
                               .maxX = 170.0f,
                               .maxY = 105.0f,
                               .maxZ = kMaxZ};
  }

  // Stage 0 (Default): Start Area merged into Winter. Starts fully uncolored.
  return SeasonColorBounds{.minX = 0.0f,
                           .minY = 0.0f,
                           .minZ = 0.0f,
                           .maxX = 0.0f,
                           .maxY = 0.0f,
                           .maxZ = 0.0f};
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
