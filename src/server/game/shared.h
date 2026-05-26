#pragma once

struct ServerGame;

// Runs every simulation tick in both Overworld and Maze (e.g. global run timer,
// RunState / GameSection / TimeComponent on the game-controller entity).
// Call from each state's update() when ready (or from a single shared hook).
void tickSharedGameLogic(ServerGame& game, float dt);
