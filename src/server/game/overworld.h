#pragma once

#include <entt/entity/fwd.hpp>

struct ServerGame;

// Minimal hub movement tick used in the main map.
void MoveInMainMap(ServerGame& game, float dt);

// True when SectionController.completed (hub can show restored color).
bool RestoreWinterColor(const ServerGame& game);
bool RestoreFallColor(const ServerGame& game);
bool RestoreSummerColor(const ServerGame& game);
bool RestoreSpringColor(const ServerGame& game);

// If the required season is completed, count OverworldTag entities with Position in
// axis-aligned bounds; when count >= requiredPlayersInZone, sets
// switch.switchOn = true.
void GatherAtExitSwitch(ServerGame& game, entt::entity switchEnt, float minX,
                        float minY, float maxX, float maxY,
                        unsigned requiredPlayersInZone,
                        shared::SectionSeasonMap requiredSeason);

// Requires switch on, parent matching door entity id, and required season to be completed.
// Opens door, unlocks the next section, and transitions season -> nextSeason.
void OpenSectionDoor(ServerGame& game, entt::entity doorEnt,
                     entt::entity switchEnt, entt::entity nextSectionEnt,
                     shared::SectionSeasonMap requiredSeason,
                     shared::SectionSeasonMap nextSeason);

// Overworld-only rules (doors, section progress, etc.).
void tickOverworldGameLogic(ServerGame& game, float dt);
