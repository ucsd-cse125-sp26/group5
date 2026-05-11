#pragma once

#include <assimp/postprocess.h>

namespace shared {

inline constexpr const char* DEFAULT_MAP_PATH = "maps/assets/landscape.glb";
inline constexpr const char* MAP_MODEL_PREFIX = "map:";

// Client and server must agree — divergent flags produce divergent vertex
// ordering and break any cross-process mesh-identity comparison.
inline constexpr unsigned int MAP_LOAD_FLAGS =
    aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs;

}  // namespace shared
