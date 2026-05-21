#pragma once

// Mirrored into GLSL as K_* macros by Shader::linkProgram. New constants
// also need a matching line in shaders.cpp::buildShaderConstantsBlock().

namespace shared {

inline constexpr int kMaxPointLights = 2;  // shadow-casting cap
inline constexpr int kMaxLightingShaderLights = 32;
inline constexpr int kPointShadowLayers = kMaxPointLights * 6;

// Linear-depth range; writer and reader must agree.
inline constexpr float kPointShadowNear = 0.1f;
inline constexpr float kPointShadowFar = 50.0f;

// Upper bound on the palette uniform array length in fragment_gbuffer.glsl.
// Setting `paletteQuantizeColors` may be any value in [0, kMaxPaletteColors].
inline constexpr int kMaxPaletteColors = 64;

}  // namespace shared
