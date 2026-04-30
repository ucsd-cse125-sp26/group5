#pragma once

#include <assimp/postprocess.h>

namespace shared {

// Path to the active map .glb. Both client and server load this from local
// disk; the server's RenderInfo entries reference sub-models the client also
// registers under MAP_MODEL_PREFIX + nodeName. Hardcoded for v1; expected to
// move into a protocol message later.
inline constexpr const char* DEFAULT_MAP_PATH = "assets/Untitled.glb";

// Prefix for sub-model keys derived from a map's node names. A map node named
// "Cube" becomes the model key "map:Cube" in Graphics::models.
inline constexpr const char* MAP_MODEL_PREFIX = "map:";

// Must match between client and server — divergent flags produce divergent
// vertex/index ordering, breaking anything that compares mesh structure
// across processes.
inline constexpr unsigned int MAP_LOAD_FLAGS =
    aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs;

}  // namespace shared
