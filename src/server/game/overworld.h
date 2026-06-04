#pragma once

#include <entt/entity/fwd.hpp>

#include "shared/components.h"

struct ServerGame;

// Minimal hub movement tick used in the main map.
void MoveInMainMap(ServerGame& game, float dt);

// True when SectionController.completed (hub can show restored color).
bool RestoreWinterColor(const ServerGame& game);
bool RestoreFallColor(const ServerGame& game);
bool RestoreSummerColor(const ServerGame& game);
bool RestoreSpringColor(const ServerGame& game);

// If the required season is completed, count OverworldTag entities with
// Position in axis-aligned bounds; when count >= requiredPlayersInZone, sets
// switch.switchOn = true.
void GatherAtExitSwitch(ServerGame& game, entt::entity switchEnt, float minX,
                        float minY, float maxX, float maxY,
                        unsigned requiredPlayersInZone,
                        shared::SectionSeasonMap requiredSeason);

// Requires switch on, parent matching door entity id, and required season to be
// completed. Opens door, unlocks the next section, and transitions season ->
// nextSeason.
void OpenSectionDoor(ServerGame& game, entt::entity doorEnt,
                     entt::entity switchEnt, entt::entity nextSectionEnt,
                     shared::SectionSeasonMap requiredSeason,
                     shared::SectionSeasonMap nextSeason);

// Attaches a shadow-casting PointLight to each revealed fragment and removes it
// once the fragment is picked up. The client filters point lights down to
// fragments and animates their color (rainbow).
void SyncFragmentLights(ServerGame& game);

// Overworld-only rules (doors, section progress, etc.).
void tickOverworldGameLogic(ServerGame& game, float dt);

// Broadcast the overworld seasonal music track matching GameSection (or
// AfterSpring when afterSpringFragmentPickup is true).
void syncOverworldSeasonMusic(ServerGame& game,
                              bool afterSpringFragmentPickup = false);

// Reveal the fragment for the current active season beside the player (~3 m).
void debugRevealActiveSeasonFragmentNearPlayer(ServerGame& game,
                                                entt::entity player);
