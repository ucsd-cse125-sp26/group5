#pragma once

#include <cstdint>

#include <entt/entity/fwd.hpp>

#include "shared/components.h"

struct ServerGame;

namespace section_puzzle {

[[nodiscard]] entt::entity findSection(const ServerGame& game,
                                       shared::SectionSeasonMap season);

[[nodiscard]] entt::entity findPuzzleForSection(const ServerGame& game,
                                                shared::SectionSeasonMap season);

[[nodiscard]] bool isSectionAvailable(const ServerGame& game,
                                      shared::SectionSeasonMap season);

[[nodiscard]] bool isSectionUnlocked(const ServerGame& game,
                                     shared::SectionSeasonMap season);

[[nodiscard]] bool isSectionCompleted(const ServerGame& game,
                                      shared::SectionSeasonMap season);

void completeSection(ServerGame& game, shared::SectionSeasonMap season);

}  // namespace section_puzzle
