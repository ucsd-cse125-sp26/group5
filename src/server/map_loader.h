#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <string>

#include "server_game.h"

// Spawns one entity per mesh-bearing glTF node and per KHR_lights_punctual
// point/directional light. Spot lights are logged and skipped. The client
// must independently load the same file via loadMapModels.
//
// `tagEntity` is invoked once per spawned entity; pass an empty function to
// leave entities untagged. Use the templated overload below to attach a
// world tag.
bool loadMap(ServerGame& game, const std::string& path,
             const std::function<void(ServerGame&, entt::entity)>& tagEntity);

template <typename WorldTag>
bool loadMap(ServerGame& game, const std::string& path) {
  return loadMap(game, path, [](ServerGame& g, entt::entity e) {
    g.registry.emplace<WorldTag>(e);
  });
}
