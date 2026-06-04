#pragma once

// Mirrored into GLSL as K_* macros by Shader::linkProgram. New constants
// also need a matching line in shaders.cpp::buildShaderConstantsBlock().

namespace shared {

inline constexpr int kMaxPointLights = 1;  // shadow-casting cap
inline constexpr int kMaxLightingShaderLights = 32;
inline constexpr int kPointShadowLayers = kMaxPointLights * 6;

// Directional cascaded-shadow-map cascade count. The directional shadow map is
// a GL_TEXTURE_2D_ARRAY with this many layers; both lighting shaders and the
// CameraBlock UBO size depend on it. Keep <= 4 (cascadeSplits is a single vec4).
inline constexpr int kShadowCascadeCount = 4;

// Linear-depth range; writer and reader must agree.
inline constexpr float kPointShadowNear = 0.1f;
inline constexpr float kPointShadowFar = 50.0f;

// Upper bound on the palette uniform array length in fragment_gbuffer.glsl.
// Setting `paletteQuantizeColors` may be any value in [0, kMaxPaletteColors].
inline constexpr int kMaxPaletteColors = 64;

// Mirrors MAX_BONES from client/asset.h — kept here so the gbuffer + shadow
// vertex shaders can declare `finalBonesMatrices[K_MAX_BONES]` consistently.
inline constexpr int kMaxBones = 100;

}  // namespace shared
