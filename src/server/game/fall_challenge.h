#pragma once

struct ServerGame;

namespace fall_challenge {

// True while the "don't get hit" falling-cube challenge is running.
[[nodiscard]] bool isActive(ServerGame& game);

// One overworld tick of the fall section. Activates the challenge once every
// connected player is inside the zone, then runs spawn -> knockback -> meter in
// the correct order. Safe to call every tick; no-ops until activated.
void update(ServerGame& game, float dt);

// Called when the player picks up the fall fragment. Marks the fall section
// and its linked puzzle as completed and increments the global section counter.
void CollectFallFragment(ServerGame& game);

}  // namespace fall_challenge