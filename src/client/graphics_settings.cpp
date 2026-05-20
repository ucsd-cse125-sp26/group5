#include "client/graphics_settings.h"

const char* presetName(GraphicsPreset p) {
  switch (p) {
    case GraphicsPreset::Default:
      return "Default";
    case GraphicsPreset::HighQuality:
      return "High Quality";
    case GraphicsPreset::Performance:
      return "Performance";
    case GraphicsPreset::CelShaded:
      return "Cel Shaded";
    case GraphicsPreset::Count:
      break;
  }
  return "?";
}

void applyPreset(GraphicsSettings& s, GraphicsPreset p) {
  s = GraphicsSettings{};
  switch (p) {
    case GraphicsPreset::Default:
      break;
    case GraphicsPreset::HighQuality:
      s.ssaoKernelSize = 64;
      s.ssaoRadius = 0.6f;
      s.bloomBlurIterations = 20;
      s.fxaaEnabled = true;
      s.dirShadowMapSize = 4096;
      s.pointShadowMapSize = 2048;
      s.dirShadowHalfExtent = 80.0f;
      break;
    case GraphicsPreset::Performance:
      s.ssaoEnabled = false;
      s.bloomEnabled = false;
      s.fxaaEnabled = false;
      s.bloomBlurIterations = 4;
      s.dirShadowMapSize = 1024;
      s.pointShadowMapSize = 512;
      s.dirShadowHalfExtent = 60.0f;
      break;
    case GraphicsPreset::CelShaded:
      s.shadingMode = ShadingMode::Cel;
      s.celBands = 4;
      s.celHalfLambert = true;
      s.celBandEpsilon = 0.02f;
      s.textureQuantizeLevels = 16;
      s.postQuantizeLevels = 16;
      s.bloomEnabled = false;
      s.fxaaEnabled = false;
      s.outlineMode = OutlineMode::Sobel;
      s.outlineColor = glm::vec3(0.0f);
      break;
    case GraphicsPreset::Count:
      break;
  }
}
