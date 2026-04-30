#pragma once

#include <string>

class ServerGame;

// Loads a glTF/glb map file and spawns one ECS entity per mesh-bearing node
// (with Position, RenderInfo, and a static PhysicsBody) plus one entity per
// KHR_lights_punctual point/directional light. Spot lights are logged and
// skipped. Returns false if the file failed to load.
//
// The client must independently load the same file and register matching
// per-node Models in Graphics::models so RenderInfo.modelName lookups resolve.
// See shared/map_format.h for the shared path constant and naming convention.
bool loadMap(ServerGame& game, const std::string& path);
