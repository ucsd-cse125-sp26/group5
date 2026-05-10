#pragma once

#include <entt/entity/fwd.hpp>

struct ServerGame;

// Minimal hub movement tick used in the main map.
void MoveInMainMap(ServerGame& game, float dt);

// True when Winter SectionController.completed (hub can show restored color).
bool RestoreWinterColor(const ServerGame& game);

// If Winter is completed, count OverworldTag entities with Position in axis-aligned bounds;
// when count >= requiredPlayersInZone, sets switch.switchOn = true.
void GatherAtExitSwitch(ServerGame& game, entt::entity switchEnt, float minX, float minY,
                        float maxX, float maxY, unsigned requiredPlayersInZone);

// Requires switch on, parent matching door entity id, Winter completed. Opens door, unlocks Fall, season -> FALL.
void OpenSectionDoor(ServerGame& game, entt::entity doorEnt, entt::entity switchEnt,
                     entt::entity fallSectionEnt);

// Overworld-only rules (doors, section progress, etc.).
void tickOverworldGameLogic(ServerGame& game, float dt);
