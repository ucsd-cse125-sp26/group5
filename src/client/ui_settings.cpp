#include "client/ui_settings.h"

#include <cstdio>

#include "client/graphics_settings.h"
#include "imgui.h"

namespace {

int currentPresetIndex = 0;

void cameraSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  ImGui::SliderFloat("FOV", &s.fovDegrees, 30.0f, 120.0f, "%.1f deg");
  ImGui::DragFloat("Near", &s.nearPlane, 0.01f, 0.01f, 10.0f, "%.3f");
  ImGui::DragFloat("Far", &s.farPlane, 1.0f, 10.0f, 2000.0f, "%.1f");
}

void shadingSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Shading")) return;
  const char* modes[] = {"Phong", "Cel"};
  int mode = static_cast<int>(s.shadingMode);
  if (ImGui::Combo("Shading mode", &mode, modes, IM_ARRAYSIZE(modes))) {
    s.shadingMode = static_cast<ShadingMode>(mode);
  }
  // 0/1 disable; 2..32 posterize each RGB channel into N levels at the
  // G-buffer sample site, so both Phong and Cel see flattened colors.
  ImGui::SliderInt("Texture quantize", &s.textureQuantizeLevels, 0, 32,
                   s.textureQuantizeLevels < 2 ? "off" : "%d levels");
  // Same algorithm but applied post-tonemap (after FXAA) — quantizes the
  // final lit color including shading and shadows, not just the source.
  ImGui::SliderInt("Post quantize", &s.postQuantizeLevels, 0, 32,
                   s.postQuantizeLevels < 2 ? "off" : "%d levels");
  // Skybox-only HSV-V quantize, applied in the skybox fragment shader.
  // Independent of texture/post quantize so the sky can posterize while
  // scene geometry stays untouched (or vice versa).
  ImGui::SliderInt("Skybox quantize", &s.skyboxQuantizeLevels, 0, 32,
                   s.skyboxQuantizeLevels < 2 ? "off" : "%d levels");
  // Scene-color palette: k-means over sampled diffuse pixels. Albedo is
  // snapped to the nearest palette entry at G-buffer time so models that
  // aren't authored with flat colors still light like they are.
  const int paletteOptions[] = {0, 8, 16, 32, 64};
  const char* paletteLabels[] = {"Off", "8", "16", "32", "64"};
  int paletteIdx = 0;
  for (int i = 0; i < IM_ARRAYSIZE(paletteOptions); ++i) {
    if (paletteOptions[i] == s.paletteQuantizeColors) paletteIdx = i;
  }
  if (ImGui::Combo("Palette quantize", &paletteIdx, paletteLabels,
                   IM_ARRAYSIZE(paletteLabels))) {
    s.paletteQuantizeColors = paletteOptions[paletteIdx];
  }
  // Skybox-only palette quantize. K-means built from sampled cubemap pixels
  // at load time and rebuilt when this value changes.
  int skyboxPaletteIdx = 0;
  for (int i = 0; i < IM_ARRAYSIZE(paletteOptions); ++i) {
    if (paletteOptions[i] == s.skyboxPaletteColors) skyboxPaletteIdx = i;
  }
  if (ImGui::Combo("Skybox palette", &skyboxPaletteIdx, paletteLabels,
                   IM_ARRAYSIZE(paletteLabels))) {
    s.skyboxPaletteColors = paletteOptions[skyboxPaletteIdx];
  }
  // Bayer ordered-dither strength for the skybox. Softens the hard plateau
  // and Voronoi-region boundaries that brightness quantize + palette snap
  // produce — 0 = crisp (default), 1 = full stipple.
  ImGui::SliderFloat("Skybox soft edge", &s.skyboxSoftEdge, 0.0f, 1.0f, "%.2f");
  // Texture filtering: 1 = current (nearest-mip), >1 = trilinear + anisotropy.
  const int anisoOptions[] = {1, 2, 4, 8, 16};
  const char* anisoLabels[] = {"Off (1x)", "2x", "4x", "8x", "16x"};
  int anisoIdx = 0;
  for (int i = 0; i < IM_ARRAYSIZE(anisoOptions); ++i) {
    if (anisoOptions[i] == s.textureAnisotropy) anisoIdx = i;
  }
  if (ImGui::Combo("Texture filtering", &anisoIdx, anisoLabels,
                   IM_ARRAYSIZE(anisoLabels))) {
    s.textureAnisotropy = anisoOptions[anisoIdx];
  }
  ImGui::BeginDisabled(s.shadingMode != ShadingMode::Cel);
  ImGui::SliderInt("Cel bands", &s.celBands, 2, 8);
  ImGui::SliderFloat("Band epsilon", &s.celBandEpsilon, 0.0f, 0.2f, "%.3f");
  ImGui::Checkbox("Half-Lambert", &s.celHalfLambert);
  ImGui::SliderFloat("Specular threshold", &s.celSpecularThreshold, 0.0f, 1.0f);
  ImGui::SliderFloat("Specular epsilon", &s.celSpecularEpsilon, 0.0f, 0.2f,
                     "%.3f");
  ImGui::Checkbox("Use ramp texture", &s.celUseRampTexture);
  ImGui::BeginDisabled(!s.celUseRampTexture);
  static char rampPathBuf[256] = "";
  // Resync from settings only when the value changed externally (e.g. preset
  // load); otherwise we would clobber what the user is typing each frame.
  static std::string rampPathLastApplied;
  if (s.celRampPath != rampPathLastApplied) {
    std::snprintf(rampPathBuf, sizeof(rampPathBuf), "%s",
                  s.celRampPath.c_str());
    rampPathLastApplied = s.celRampPath;
  }
  if (ImGui::InputText("Ramp PNG path", rampPathBuf, sizeof(rampPathBuf),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    s.celRampPath = rampPathBuf;
    rampPathLastApplied = s.celRampPath;
  }
  ImGui::EndDisabled();
  ImGui::SeparatorText("Rim light");
  ImGui::SliderFloat("Rim strength", &s.celRimStrength, 0.0f, 2.0f, "%.2f");
  ImGui::BeginDisabled(s.celRimStrength <= 0.0f);
  ImGui::ColorEdit3("Rim color", &s.celRimColor.x);
  ImGui::SliderFloat("Rim power", &s.celRimPower, 0.5f, 16.0f, "%.2f");
  ImGui::SliderFloat("Rim threshold", &s.celRimThreshold, 0.0f, 1.0f, "%.2f");
  ImGui::EndDisabled();
  ImGui::EndDisabled();
}

void outlinesSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Outlines")) return;
  const char* modes[] = {"None", "Sobel", "Cross"};
  int mode = static_cast<int>(s.outlineMode);
  if (ImGui::Combo("Outline mode", &mode, modes, IM_ARRAYSIZE(modes))) {
    s.outlineMode = static_cast<OutlineMode>(mode);
  }
  ImGui::ColorEdit3("Outline color", &s.outlineColor.x);

  const bool sobelActive = s.outlineMode == OutlineMode::Sobel;
  const bool crossActive = s.outlineMode == OutlineMode::Cross;
  ImGui::BeginDisabled(!sobelActive);
  ImGui::SliderFloat("Sobel width", &s.outlineSobelWidth, 0.5f, 5.0f);
  ImGui::EndDisabled();
  // Both modes interpret this as a relative threshold (Δd / d) and a
  // 1 - dot(n, n') normal jump, so the same slider value works across modes.
  ImGui::BeginDisabled(!sobelActive && !crossActive);
  ImGui::SliderFloat("Depth threshold", &s.outlineDepthThreshold, 0.0f, 0.5f,
                     "%.4f");
  ImGui::SliderFloat("Normal threshold", &s.outlineNormalThreshold, 0.0f, 4.0f,
                     "%.3f");
  ImGui::EndDisabled();
}

void dirLightSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Directional Light")) return;
  // Image-based-ambient approximation (skybox average tint). 0 = flat ambient.
  ImGui::SliderFloat("Sky ambient (IBL)", &s.iblAmbientStrength, 0.0f, 1.0f,
                     "%.2f");
  ImGui::Checkbox("Override scene light", &s.overrideDirLight);
  ImGui::BeginDisabled(!s.overrideDirLight);
  ImGui::DragFloat3("Direction", &s.dirLightDirection.x, 0.01f, -1.0f, 1.0f);
  ImGui::ColorEdit3("Ambient", &s.dirLightAmbient.x);
  ImGui::ColorEdit3("Diffuse", &s.dirLightDiffuse.x);
  ImGui::ColorEdit3("Specular", &s.dirLightSpecular.x);
  ImGui::EndDisabled();
}

void tonemapBloomSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Tonemap & Bloom",
                               ImGuiTreeNodeFlags_DefaultOpen))
    return;
  ImGui::SliderFloat("Exposure", &s.exposure, 0.1f, 5.0f);
  const char* tonemapModes[] = {"Exponential", "ACES", "AgX"};
  ImGui::Combo("Tonemap", &s.tonemapMode, tonemapModes,
               IM_ARRAYSIZE(tonemapModes));
  ImGui::Checkbox("Bloom", &s.bloomEnabled);
  ImGui::BeginDisabled(!s.bloomEnabled);
  ImGui::SliderFloat("Threshold", &s.bloomThreshold, 0.0f, 5.0f);
  ImGui::SliderFloat("Strength", &s.bloomStrength, 0.0f, 5.0f);
  ImGui::Checkbox("Mip-chain", &s.bloomMipChain);
  ImGui::BeginDisabled(s.bloomMipChain);
  ImGui::SliderInt("Blur iterations", &s.bloomBlurIterations, 0, 30);
  ImGui::EndDisabled();
  ImGui::EndDisabled();
}

void ssaoSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("SSAO")) return;
  ImGui::Checkbox("SSAO enabled", &s.ssaoEnabled);
  ImGui::BeginDisabled(!s.ssaoEnabled);
  ImGui::SliderInt("Kernel size", &s.ssaoKernelSize, 8, 64);
  ImGui::SliderFloat("Radius", &s.ssaoRadius, 0.05f, 2.0f);
  ImGui::SliderFloat("Bias", &s.ssaoBias, 0.0f, 0.1f, "%.4f");
  const int scales[] = {1, 2, 4};
  const char* scaleLabels[] = {"Full", "Half", "Quarter"};
  int scaleIdx = 0;
  for (int i = 0; i < IM_ARRAYSIZE(scales); ++i) {
    if (scales[i] == s.ssaoScale) scaleIdx = i;
  }
  if (ImGui::Combo("Resolution", &scaleIdx, scaleLabels,
                   IM_ARRAYSIZE(scaleLabels))) {
    s.ssaoScale = scales[scaleIdx];
  }
  ImGui::Checkbox("Bilateral blur", &s.ssaoBilateralBlur);
  ImGui::SliderFloat("Power", &s.ssaoPower, 1.0f, 4.0f, "%.2f");
  ImGui::EndDisabled();
}

void shadowsSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Shadows")) return;
  ImGui::Checkbox("Shadows enabled", &s.shadowsEnabled);
  ImGui::BeginDisabled(!s.shadowsEnabled);
  ImGui::Checkbox("Point shadows enabled", &s.pointShadowsEnabled);

  const int dirSizes[] = {512, 1024, 2048, 4096};
  int dirIdx = 0;
  for (int i = 0; i < IM_ARRAYSIZE(dirSizes); ++i) {
    if (dirSizes[i] == s.dirShadowMapSize) dirIdx = i;
  }
  const char* dirLabels[] = {"512", "1024", "2048", "4096"};
  if (ImGui::Combo("Dir map size", &dirIdx, dirLabels,
                   IM_ARRAYSIZE(dirLabels))) {
    s.dirShadowMapSize = dirSizes[dirIdx];
  }

  const int pointSizes[] = {256, 512, 1024, 2048};
  int pointIdx = 0;
  for (int i = 0; i < IM_ARRAYSIZE(pointSizes); ++i) {
    if (pointSizes[i] == s.pointShadowMapSize) pointIdx = i;
  }
  const char* pointLabels[] = {"256", "512", "1024", "2048"};
  if (ImGui::Combo("Point map size", &pointIdx, pointLabels,
                   IM_ARRAYSIZE(pointLabels))) {
    s.pointShadowMapSize = pointSizes[pointIdx];
  }

  ImGui::SliderFloat("Dir half-extent", &s.dirShadowHalfExtent, 10.0f, 1000.0f);
  ImGui::SliderFloat("Dir back distance", &s.dirShadowBackDistance, 10.0f,
                     1500.0f);
  ImGui::SliderFloat("Dir far plane", &s.dirShadowFarPlane, 50.0f, 4000.0f);
  ImGui::SliderFloat("Dir bias factor", &s.dirShadowPolyFactor, 0.0f, 10.0f);
  ImGui::SliderFloat("Dir bias units", &s.dirShadowPolyUnits, 0.0f, 20.0f);
  ImGui::SliderFloat("Point far plane", &s.pointShadowFarPlane, 5.0f, 200.0f);
  ImGui::SliderFloat("Point bias factor", &s.pointShadowPolyFactor, 0.0f,
                     10.0f);
  ImGui::SliderFloat("Point bias units", &s.pointShadowPolyUnits, 0.0f, 20.0f);
  ImGui::SliderFloat("Alpha cutoff", &s.shadowAlphaCutoff, 0.0f, 1.0f);
  ImGui::SliderInt("PCF radius", &s.shadowPcfRadius, 1, 4);
  ImGui::SliderFloat("Softness", &s.shadowSoftness, 1.0f, 4.0f, "%.2f");
  ImGui::EndDisabled();
}

void antialiasingSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Antialiasing")) return;
  ImGui::Checkbox("FXAA", &s.fxaaEnabled);
  ImGui::BeginDisabled(!s.fxaaEnabled);
  const char* fxaaQualities[] = {"Standard", "High"};
  ImGui::Combo("FXAA quality", &s.fxaaQuality, fxaaQualities,
               IM_ARRAYSIZE(fxaaQualities));
  ImGui::EndDisabled();
  // Ordered dither to break HDR->8-bit banding; independent of FXAA.
  ImGui::SliderFloat("Dither", &s.ditherStrength, 0.0f, 1.0f, "%.2f");
}

void overlaySection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Overlays")) return;
  ImGui::Checkbox("Show FPS", &s.showFPS);
  // Per-pass GPU timing HUD. Issues GL timer queries only while enabled.
  ImGui::Checkbox("GPU perf HUD", &s.showPerfHUD);
}

void performanceSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Performance")) return;
  // Cull the main G-buffer + directional shadow passes to the camera/light
  // frustum. Off reproduces the current (cull-nothing) behavior.
  ImGui::Checkbox("Main-pass frustum culling", &s.mainFrustumCulling);
  // Cache resolved Model* per entity to skip per-frame key string building.
  ImGui::Checkbox("Cache model lookups", &s.cacheModelLookup);
}

void colorRestorationSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Color Restoration")) return;
  ImGui::Checkbox("Enabled", &s.colorRestorationEnabled);
  ImGui::BeginDisabled(!s.colorRestorationEnabled);
  ImGui::SliderFloat("Outside strength", &s.colorRestorationStrength, 0.0f,
                     1.0f);
  ImGui::SliderFloat("Edge width (world)", &s.colorRestorationEdgeWidth, 0.0f,
                     20.0f);
  ImGui::SliderFloat("Colored light keep", &s.colorRestorationLightStrength,
                     0.0f, 1.0f);
  ImGui::EndDisabled();
}

void pixelationSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Pixelation")) return;
  // 1 = native; >=2 renders the 3D scene at fb/N and upscales with NEAREST.
  const int scales[] = {1, 2, 3, 4, 6, 8};
  const char* labels[] = {"Off", "2x", "3x", "4x", "6x", "8x"};
  int idx = 0;
  for (int i = 0; i < IM_ARRAYSIZE(scales); ++i) {
    if (scales[i] == s.pixelationScale) idx = i;
  }
  if (ImGui::Combo("Scale", &idx, labels, IM_ARRAYSIZE(labels))) {
    s.pixelationScale = scales[idx];
  }
}

}  // namespace

void drawSettingsUI(GraphicsSettings& s, bool& open) {
  ImGui::SetNextWindowSize(ImVec2(420, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Graphics Settings", &open)) {
    ImGui::End();
    return;
  }

  const int kPresetCount = static_cast<int>(GraphicsPreset::Count);
  const char* preview =
      presetName(static_cast<GraphicsPreset>(currentPresetIndex));
  if (ImGui::BeginCombo("Preset", preview)) {
    for (int i = 0; i < kPresetCount; ++i) {
      bool selected = (currentPresetIndex == i);
      if (ImGui::Selectable(presetName(static_cast<GraphicsPreset>(i)),
                            selected)) {
        currentPresetIndex = i;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Apply")) {
    applyPreset(s, static_cast<GraphicsPreset>(currentPresetIndex));
  }

  ImGui::Separator();

  cameraSection(s);
  shadingSection(s);
  outlinesSection(s);
  dirLightSection(s);
  tonemapBloomSection(s);
  ssaoSection(s);
  shadowsSection(s);
  antialiasingSection(s);
  pixelationSection(s);
  colorRestorationSection(s);
  performanceSection(s);
  overlaySection(s);

  ImGui::Separator();
  if (ImGui::Button("Reset to Default")) {
    applyPreset(s, GraphicsPreset::Default);
    currentPresetIndex = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button("Close (H)")) {
    open = false;
  }

  ImGui::End();
}
