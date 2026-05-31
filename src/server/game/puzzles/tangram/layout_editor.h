#pragma once

#include <vector>

#include "server/scene.h"
#include "shared/puzzles/tangram/arena_layout.h"

struct ServerGame;

namespace tangram_layout_editor {

std::vector<StaticEntityDesc> buildArenaEntities(
    const shared::tangram::ArenaLayout& layout);

std::vector<StaticEntityDesc> buildTriggerMarkerEntities(
    const shared::tangram::ArenaLayout& layout);

void spawnLayoutVisuals(ServerGame& game);

}  // namespace tangram_layout_editor
