#pragma once

#include <vector>

#include "server/scene.h"
#include "shared/puzzles/maze/layout.h"

struct ServerGame;

namespace maze_layout_editor {

std::vector<StaticEntityDesc> buildPreviewEntities(
    const shared::maze_layout::Config& layout);

std::vector<StaticEntityDesc> buildTriggerMarkerEntities(
    const shared::maze_layout::Config& layout);

void spawnLayoutVisuals(ServerGame& game);

void despawnLayoutVisuals(ServerGame& game);

// Updates server layout and respawns pad + preview tiles (not player spawn).
void applyLayout(ServerGame& game, shared::maze_layout::Config layout);

void printLayoutSnippet(const shared::maze_layout::Config& layout);

}  // namespace maze_layout_editor
