#pragma once

#include <string>

class ServerGame;

// Spawns one entity per mesh-bearing glTF node and per KHR_lights_punctual
// point/directional light. Spot lights are logged and skipped. The client
// must independently load the same file via loadMapModels.
bool loadMap(ServerGame& game, const std::string& path);
