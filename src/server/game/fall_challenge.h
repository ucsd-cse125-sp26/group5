#pragma once

struct ServerGame;

namespace fall_challenge {

// True while the "don't get hit" falling-cube challenge is running.
[[nodiscard]] bool isActive(ServerGame& game);

// One overworld tick of the fall section. Activates the challenge once every
// connected player is inside the zone, then runs spawn -> knockback -> meter in
// the correct order. Safe to call every tick; no-ops until activated.
void update(ServerGame& game, float dt);

}  // namespace fall_challenge