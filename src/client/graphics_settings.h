#pragma once

#include <string>

#include "glm/ext/vector_float3.hpp"

enum class ShadingMode {
  Phong,
  Cel,
};

enum class OutlineMode {
  None,
  // Post-process Sobel: 3x3 luma + gNormal/gPosition probe in its own pass.
  // Most modular but reads the g-buffer one more time.
  Sobel,
  // 4-tap normal+depth probe folded into the deferred lighting pass.
  // Cheaper than Sobel and shares the gNormal/gPosition fetches already
  // happening in lighting, at the cost of coupling outline detection to
  // whichever lighting shader is active.
  Cross,
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
  // SSAO render resolution = renderWidth / ssaoScale (1 = full, 2 = half,
  // 4 = quarter). Lighting samples ssaoBlurColor through bilinear filtering
  // when scale > 1.
  int ssaoScale = 1;

  // FXAA
  bool fxaaEnabled = true;

  // Top-left overlay showing ImGui's smoothed framerate.
  bool showFPS = true;

  // Pixelation: render the entire 3D scene at fb/scale, then upscale with
  // GL_NEAREST in the present pass for a chunky-pixel look. 1 = off.
  int pixelationScale = 1;

  // Color restoration: desaturate fragments outside the local player's
  // shared::ColorBoundingBox in the tonemap pass. The server grows the box
  // as sections are completed, so "restored color" tracks game progress.
  bool colorRestorationEnabled = false;
  // 0 = no effect, 1 = full grayscale outside the box.
  float colorRestorationStrength = 1.0f;
  // Soft edge in world units to avoid a hard color/gray boundary.
  float colorRestorationEdgeWidth = 1.0f;

  // Shadows
  bool shadowsEnabled = true;
  bool pointShadowsEnabled = false;
  // Map sizes — changing triggers FBO/texture reallocation.
  int dirShadowMapSize = 2048;
  int pointShadowMapSize = 512;
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
  // 0 = off, otherwise the size of a k-means palette built once from sampled
  // diffuse-texture pixels. When > 0, the G-buffer pass snaps albedo to the
  // nearest palette entry (linear-RGB nearest neighbour). Capped at
  // shared::kMaxPaletteColors.
  int paletteQuantizeColors = 0;
  // 0 = off, otherwise levels for HSV-V quantization of the final tonemapped
  // color in the present shader (after FXAA so band edges stay sharp).
  int postQuantizeLevels = 0;
  // 0 = off, otherwise HSV-V quantization applied to the skybox fragment
  // only, independent of texture/post quantize. Lets the sky posterize
  // without affecting scene geometry.
  int skyboxQuantizeLevels = 0;
  // 0 = off, otherwise k-means palette over sampled cubemap face pixels.
  // Same algorithm as paletteQuantizeColors but built per-skybox and bound
  // only by the skybox fragment shader. Capped at shared::kMaxPaletteColors.
  int skyboxPaletteColors = 0;
  // 0 = off, 1 = max softening. Bayer 4×4 dither offset applied before
  // skybox brightness quantize and palette snap, so hard plateau / region
  // boundaries break into a stippled transition instead of a sharp line.
  float skyboxSoftEdge = 0.0f;
  int celBands = 4;
  float celBandEpsilon = 0.02f;  // 0 = hard, ~0.1 = soft
  bool celHalfLambert = true;
  float celSpecularThreshold = 0.5f;
  float celSpecularEpsilon = 0.05f;
  bool celUseRampTexture = false;
  std::string celRampPath = "";  // empty → procedural

  // Outlines (post-process Sobel only)
  OutlineMode outlineMode = OutlineMode::Cross;
  glm::vec3 outlineColor{0.0f};
  // Depth threshold is relative (Δd / d), normal threshold is sum of 4
  // neighbor (1 - dot(n, nNeighbor)).
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
