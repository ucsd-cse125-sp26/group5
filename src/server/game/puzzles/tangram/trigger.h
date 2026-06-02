#pragma once

#include <glm/vec3.hpp>
#include <vector>

#include "server/scene.h"
#include "shared/components.h"
#include "shared/puzzles/tangram/arena_layout.h"

struct ServerGame;

namespace tangram_trigger {

[[nodiscard]] bool isInsideTypingTriggerRegion(
    const shared::Position& position,
    const shared::tangram::ArenaLayout& layout);

[[nodiscard]] bool isInsideTypingTriggerRegion(const shared::Position& position,
                                               const ServerGame& game);

[[nodiscard]] bool allActivePlayersInTangramTrigger(const ServerGame& game);

[[nodiscard]] bool isFallTypingAvailable(const ServerGame& game);

// Tangram can replay after a win; only requires the section to be unlocked.
[[nodiscard]] bool canTriggerTangram(const ServerGame& game);

[[nodiscard]] glm::vec3 overworldSpawnPosition(
    const shared::tangram::ArenaLayout& layout, uint8_t joinSlot);

[[nodiscard]] glm::vec3 overworldSpawnPosition(const ServerGame& game,
                                               uint8_t joinSlot);

// Dev/local test: move all connected overworld avatars onto the tangram pad.
void snapAllPlayersToTangramPad(ServerGame& game);

// Prevents avatars from falling off the sky tangram table into the void.
void keepPlayersOnTangramPlatform(ServerGame& game);

}  // namespace tangram_trigger
