#include "client/ui_settings.h"

#include <cstdio>

#include "client/graphics_settings.h"
#include "imgui.h"

namespace {

int currentPresetIndex = 0;

void cameraSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) return;
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
  if (rampPathBuf[0] == '\0' && !s.celRampPath.empty()) {
    std::snprintf(rampPathBuf, sizeof(rampPathBuf), "%s", s.celRampPath.c_str());
  }
  if (ImGui::InputText("Ramp PNG path", rampPathBuf, sizeof(rampPathBuf),
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
    s.celRampPath = rampPathBuf;
  }
  ImGui::EndDisabled();
  ImGui::EndDisabled();
}

void outlinesSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Outlines")) return;
  const char* modes[] = {"None", "Hull", "Sobel", "Both"};
  int mode = static_cast<int>(s.outlineMode);
  if (ImGui::Combo("Outline mode", &mode, modes, IM_ARRAYSIZE(modes))) {
    s.outlineMode = static_cast<OutlineMode>(mode);
  }
  ImGui::ColorEdit3("Outline color", &s.outlineColor.x);

  const bool hullActive = s.outlineMode == OutlineMode::Hull ||
                          s.outlineMode == OutlineMode::Both;
  ImGui::BeginDisabled(!hullActive);
  ImGui::SliderFloat("Hull thickness", &s.outlineThickness, 0.0f, 0.2f,
                     "%.4f");
  ImGui::Checkbox("Screen-constant", &s.outlineScreenSpace);
  ImGui::EndDisabled();

  const bool sobelActive = s.outlineMode == OutlineMode::Sobel ||
                           s.outlineMode == OutlineMode::Both;
  ImGui::BeginDisabled(!sobelActive);
  ImGui::SliderFloat("Sobel width", &s.outlineSobelWidth, 0.5f, 5.0f);
  ImGui::SliderFloat("Depth threshold", &s.outlineDepthThreshold, 0.0f, 0.5f,
                     "%.4f");
  ImGui::SliderFloat("Normal threshold", &s.outlineNormalThreshold, 0.0f, 4.0f,
                     "%.3f");
  ImGui::EndDisabled();
}

void dirLightSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Directional Light")) return;
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
  ImGui::Checkbox("Bloom", &s.bloomEnabled);
  ImGui::BeginDisabled(!s.bloomEnabled);
  ImGui::SliderFloat("Threshold", &s.bloomThreshold, 0.0f, 5.0f);
  ImGui::SliderFloat("Strength", &s.bloomStrength, 0.0f, 5.0f);
  ImGui::SliderInt("Blur iterations", &s.bloomBlurIterations, 0, 30);
  ImGui::EndDisabled();
}

void ssaoSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("SSAO")) return;
  ImGui::Checkbox("SSAO enabled", &s.ssaoEnabled);
  ImGui::BeginDisabled(!s.ssaoEnabled);
  ImGui::SliderInt("Kernel size", &s.ssaoKernelSize, 8, 64);
  ImGui::SliderFloat("Radius", &s.ssaoRadius, 0.05f, 2.0f);
  ImGui::SliderFloat("Bias", &s.ssaoBias, 0.0f, 0.1f, "%.4f");
  ImGui::EndDisabled();
}

void shadowsSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Shadows")) return;
  ImGui::Checkbox("Shadows enabled", &s.shadowsEnabled);
  ImGui::BeginDisabled(!s.shadowsEnabled);

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

  ImGui::SliderFloat("Dir half-extent", &s.dirShadowHalfExtent, 10.0f,
                     1000.0f);
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
  ImGui::EndDisabled();
}

void antialiasingSection(GraphicsSettings& s) {
  if (!ImGui::CollapsingHeader("Antialiasing")) return;
  ImGui::Checkbox("FXAA", &s.fxaaEnabled);
}

}  // namespace

void drawSettingsUI(GraphicsSettings& s, bool& open) {
  ImGui::SetNextWindowSize(ImVec2(420, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Graphics Settings", &open)) {
    ImGui::End();
    return;
  }

  const int kPresetCount = static_cast<int>(GraphicsPreset::Count);
  const char* preview = presetName(static_cast<GraphicsPreset>(currentPresetIndex));
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

  ImGui::Separator();
  if (ImGui::Button("Reset to Default")) {
    applyPreset(s, GraphicsPreset::Default);
    currentPresetIndex = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button("Close (Esc)")) {
    open = false;
  }

  ImGui::End();
}
