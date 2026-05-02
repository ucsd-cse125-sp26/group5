#pragma once

// Constants shared between C++ and GLSL. Values are mirrored into every
// shader at compile time by Shader::linkProgram (see shaders.cpp); GLSL
// references them as preprocessor macros prefixed K_*.
//
// Adding a new constant: declare it here, then add a matching line to the
// generator in shaders.cpp::buildShaderConstantsBlock().

namespace shared {

// Maximum number of shadow-casting point lights. The cubemap-array shadow
// target reserves 6 layers per light, so kPointShadowLayers = kMaxPointLights
// * 6.
inline constexpr int kMaxPointLights = 4;

// Maximum total point lights consumed by the deferred lighting shader.
// Lights beyond kMaxPointLights are non-shadow-casting.
inline constexpr int kMaxLightingShaderLights = 32;

// Cubemap-array depth layers for point-light shadows.
inline constexpr int kPointShadowLayers = kMaxPointLights * 6;

// Linear-depth encoding range used by the point-light shadow pass and read
// by the lighting pass. Must match between writer and reader.
inline constexpr float kPointShadowNear = 0.1f;
inline constexpr float kPointShadowFar = 50.0f;

}  // namespace shared
