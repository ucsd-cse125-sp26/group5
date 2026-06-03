#pragma once

struct ServerGame;

namespace summer_escape {

// True while the shrinking-zone escape run is active.
[[nodiscard]] bool isActive(ServerGame& game);

// One overworld tick of the summer section. Activates the run once every
// connected player stands on the summer pad (and the section is unlocked),
// then drives the wave shrink / restart / solve logic. Safe to call every
// tick; no-ops until activated.
void update(ServerGame& game, float dt);

// Called when a player picks up the revealed summer fragment. Marks the summer
// section and its linked puzzle completed and bumps the global section counter.
void CollectSummerFragment(ServerGame& game);

// Debug: move every connected overworld player onto the summer pad (spread by
// slot). Requires debug input (V) on the client.
void debugSnapAllPlayersToSummerPad(ServerGame& game);

}  // namespace summer_escape
