#pragma once

#include "glm/ext/vector_float3.hpp"

enum class ShadingMode {
  Phong,
  Cel,
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
  float dirShadowHalfExtent = 80.0f;
  float dirShadowBackDistance = 120.0f;
  float dirShadowFarPlane = 320.0f;
  float dirShadowPolyFactor = 2.0f;
  float dirShadowPolyUnits = 4.0f;
  // Point shadow projection + bias.
  float pointShadowFarPlane = 50.0f;
  float pointShadowPolyFactor = 2.0f;
  float pointShadowPolyUnits = 4.0f;

  // Shading (cel slots wired in UI; shader work is future)
  ShadingMode shadingMode = ShadingMode::Phong;
  int celBands = 4;
  float outlineWidth = 0.0f;
  glm::vec3 outlineColor{0.0f};

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
