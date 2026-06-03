#pragma once

#include <cstdint>
#include <entt/entity/fwd.hpp>

#include "shared/components.h"

struct ServerGame;

namespace section_puzzle {

[[nodiscard]] entt::entity findSection(const ServerGame& game,
                                       shared::SectionSeasonMap season);

[[nodiscard]] entt::entity findPuzzleForSection(
    const ServerGame& game, shared::SectionSeasonMap season);

[[nodiscard]] bool isSectionAvailable(const ServerGame& game,
                                      shared::SectionSeasonMap season);

[[nodiscard]] bool isSectionUnlocked(const ServerGame& game,
                                     shared::SectionSeasonMap season);

[[nodiscard]] bool isSectionCompleted(const ServerGame& game,
                                      shared::SectionSeasonMap season);

void completeSection(ServerGame& game, shared::SectionSeasonMap season);

// Skybox scene each season maps to. Drives both the cubemap selection and
// the SceneInfo directional-light defaults on the client (no ECS
// DirectionalLight is emplaced, so the client always falls through to
// SceneInfo).
[[nodiscard]] const char* sceneNameForSeason(shared::SectionSeasonMap season);

// Sets GameSection.currentActiveSeason for all GameSection entities AND
// rewrites the overworld light entity's Scene.name so the cubemap + light
// follow the season. Per-tick UPDATE_ENTITY broadcast carries the Scene
// component, so clients pick the new scene next frame.
void setActiveSeason(ServerGame& game, shared::SectionSeasonMap season);

}  // namespace section_puzzle
