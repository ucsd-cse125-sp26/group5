#pragma once

#include <string>

#include "glm/ext/vector_float3.hpp"

enum class ShadingMode {
  Phong,
  Cel,
};

enum class OutlineMode {
  None,
  Hull,
  Sobel,
  Both,
};

struct GraphicsSettings {
  // Camera
  float fovDegrees = 45.0f;
  float nearPlane = 0.1f;
  float farPlane = 500.0f;

  // Tonemap / bloom
  float exposure = 1.0f;
  bool bloomEnabled = true;
  float bloomThreshold = 1.0f;
  float bloomStrength = 1.0f;
  int bloomBlurIterations = 10;

  // SSAO
  bool ssaoEnabled = true;
  int ssaoKernelSize = 64;
  float ssaoRadius = 0.5f;
  float ssaoBias = 0.025f;

  // FXAA
  bool fxaaEnabled = true;

  // Shadows
  bool shadowsEnabled = true;
  // Map sizes — changing triggers FBO/texture reallocation.
  int dirShadowMapSize = 2048;
  int pointShadowMapSize = 1024;
  // Directional ortho frustum + depth range.
  float dirShadowHalfExtent = 400.0f;
  float dirShadowBackDistance = 600.0f;
  float dirShadowFarPlane = 1600.0f;
  float dirShadowPolyFactor = 2.0f;
  float dirShadowPolyUnits = 4.0f;
  // Point shadow projection + bias.
  float pointShadowFarPlane = 50.0f;
  float pointShadowPolyFactor = 2.0f;
  float pointShadowPolyUnits = 4.0f;
  // Diffuse alpha below this is discarded during shadow passes so cutout
  // meshes (foliage, fences) cast matching-shape shadows. 0 = no cutout.
  float shadowAlphaCutoff = 0.5f;

  // Shading
  ShadingMode shadingMode = ShadingMode::Phong;
  // 0 = no quantization, otherwise per-channel level count applied to the
  // material textures (albedo/specular/emissive) at G-buffer time.
  int textureQuantizeLevels = 0;
  // 0 = off, otherwise levels for HSV-V quantization of the final tonemapped
  // color in the present shader (after FXAA so band edges stay sharp).
  int postQuantizeLevels = 0;
  int celBands = 4;
  float celBandEpsilon = 0.02f;          // 0 = hard, ~0.1 = soft
  bool celHalfLambert = true;
  float celSpecularThreshold = 0.5f;
  float celSpecularEpsilon = 0.05f;
  bool celUseRampTexture = false;
  std::string celRampPath = "";          // empty → procedural

  // Outlines
  OutlineMode outlineMode = OutlineMode::None;
  glm::vec3 outlineColor{0.0f};
  // Inverted hull
  float outlineThickness = 0.02f;
  bool outlineScreenSpace = true;
  // Post-process Sobel — depth threshold is relative (Δd / d), normal
  // threshold is sum of 4 neighbor (1 - dot(n, nNeighbor)).
  float outlineSobelWidth = 1.0f;
  float outlineDepthThreshold = 0.05f;
  float outlineNormalThreshold = 0.5f;

  // Directional-light override (off → scene/ECS values used)
  bool overrideDirLight = false;
  glm::vec3 dirLightDirection{0.3f, 1.0f, -0.4f};
  glm::vec3 dirLightAmbient{0.2f, 0.2f, 0.2f};
  glm::vec3 dirLightDiffuse{0.8f, 0.8f, 0.8f};
  glm::vec3 dirLightSpecular{1.0f, 1.0f, 1.0f};
};

enum class GraphicsPreset {
  Default,
  HighQuality,
  Performance,
  CelShaded,
  Count,
};

void applyPreset(GraphicsSettings& s, GraphicsPreset p);
const char* presetName(GraphicsPreset p);
