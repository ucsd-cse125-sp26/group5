#pragma once
#include <string>
#include <vector>

#include "server_game.h"

struct StaticEntityDesc {
  float x, y, z;
  std::string modelName;
  float scale;
  std::string meshPath;
  bool render = true;  // default true, set false for physics-only
  float halfX = -1.0f, halfY = -1.0f, halfZ = -1.0f;
};

void spawnStaticEntities(ServerGame& game,
                         const std::vector<StaticEntityDesc>& descs);