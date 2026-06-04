#pragma once

struct ServerGame;

namespace server_debug {

// Drains ServerGame::pendingDebugCommands and executes each one. Called once
// per server poll from the fixed-step loop (game thread) so debug actions —
// entity spawns, Jolt body moves, packet broadcasts — run at a safe point,
// matching where the old keys_newly_pressed debug logic used to run.
void processPendingCommands(ServerGame& game);

}  // namespace server_debug
