#pragma once

#include <entt/entity/fwd.hpp>

struct ServerGame;

namespace maze_spirit_control {

// One shared green maze piece; all maze players can push it with arrow keys.
struct SpiritDrive {
  float dx = 0.0f;
  float dy = 0.0f;
  int activePushCount = 0;
};

[[nodiscard]] entt::entity findSharedSpirit(const ServerGame& game);

// Sum world-space push from each maze player's held arrow keys.
[[nodiscard]] SpiritDrive collectSpiritDriveFromPlayers(const ServerGame& game);

// Sum arrow keys from overworld cube avatars during the preview-board puzzle.
[[nodiscard]] SpiritDrive collectSpiritDriveFromOverworldPlayers(
    const ServerGame& game);

// Sets the visible green piece's physics velocity from normalized drive.
void applySpiritDriveVelocity(ServerGame& game, entt::entity spirit,
                              const SpiritDrive& drive);

}  // namespace maze_spirit_control
