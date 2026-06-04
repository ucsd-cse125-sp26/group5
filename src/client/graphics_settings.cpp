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
    case GraphicsPreset::DefaultNew:
      return "Default (New)";
    case GraphicsPreset::HighQualityNew:
      return "High Quality (New)";
    case GraphicsPreset::PerformanceNew:
      return "Performance (New)";
    case GraphicsPreset::CelShadedNew:
      return "Cel Shaded (New)";
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
      s.dirShadowHalfExtent = 500.0f;
      break;
    case GraphicsPreset::Performance:
      s.ssaoEnabled = false;
      s.bloomEnabled = false;
      s.fxaaEnabled = false;
      s.bloomBlurIterations = 4;
      s.dirShadowMapSize = 1024;
      s.pointShadowMapSize = 512;
      s.dirShadowHalfExtent = 300.0f;
      break;
    case GraphicsPreset::CelShaded:
      s.shadingMode = ShadingMode::Cel;
      s.celBands = 4;
      s.celHalfLambert = true;
      s.celBandEpsilon = 0.02f;
      s.textureQuantizeLevels = 16;
      s.postQuantizeLevels = 16;
      s.paletteQuantizeColors = 8;
      s.bloomEnabled = false;
      s.fxaaEnabled = false;
      s.ssaoEnabled = false;
      s.colorRestorationEnabled = true;
      s.outlineMode = OutlineMode::Cross;
      s.outlineColor = glm::vec3(0.0f);
      break;
    // "(New)" presets: start from the matching base preset, then enable the
    // new toggleable features tuned for that tier. Recursing into applyPreset
    // re-resets `s` and applies the base, so these stay in sync automatically.
    case GraphicsPreset::DefaultNew:
      applyPreset(s, GraphicsPreset::Default);
      s.tonemapMode = 1;  // ACES
      s.textureAnisotropy = 8;
      s.ditherStrength = 1.0f;
      s.ssaoBilateralBlur = true;
      s.mainFrustumCulling = true;
      s.cacheModelLookup = true;
      s.bloomMipChain = true;
      break;
    case GraphicsPreset::HighQualityNew:
      applyPreset(s, GraphicsPreset::HighQuality);
      s.tonemapMode = 1;  // ACES
      s.textureAnisotropy = 16;
      s.ditherStrength = 1.0f;
      s.ssaoBilateralBlur = true;
      s.mainFrustumCulling = true;
      s.cacheModelLookup = true;
      s.bloomMipChain = true;
      s.shadowPcfRadius = 2;
      s.shadowSoftness = 1.5f;
      break;
    case GraphicsPreset::PerformanceNew:
      applyPreset(s, GraphicsPreset::Performance);
      s.mainFrustumCulling = true;
      s.cacheModelLookup = true;
      break;
    case GraphicsPreset::CelShadedNew:
      applyPreset(s, GraphicsPreset::CelShaded);
      s.tonemapMode = 1;  // ACES
      s.textureAnisotropy = 8;
      s.ditherStrength = 1.0f;
      s.mainFrustumCulling = true;
      s.cacheModelLookup = true;
      s.celRimStrength = 0.5f;
      break;
    case GraphicsPreset::Count:
      break;
  }
}
