#include "client/ui_settings.h"

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
  if (ImGui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
    s.shadingMode = static_cast<ShadingMode>(mode);
  }
  ImGui::BeginDisabled(s.shadingMode == ShadingMode::Phong);
  ImGui::SliderInt("Cel bands", &s.celBands, 2, 8);
  ImGui::EndDisabled();
  ImGui::SliderFloat("Outline width", &s.outlineWidth, 0.0f, 5.0f);
  ImGui::ColorEdit3("Outline color", &s.outlineColor.x);
  ImGui::TextDisabled("(Cel shader not wired yet)");
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

  ImGui::SliderFloat("Dir half-extent", &s.dirShadowHalfExtent, 10.0f, 200.0f);
  ImGui::SliderFloat("Dir back distance", &s.dirShadowBackDistance, 10.0f,
                     400.0f);
  ImGui::SliderFloat("Dir far plane", &s.dirShadowFarPlane, 50.0f, 1000.0f);
  ImGui::SliderFloat("Dir bias factor", &s.dirShadowPolyFactor, 0.0f, 10.0f);
  ImGui::SliderFloat("Dir bias units", &s.dirShadowPolyUnits, 0.0f, 20.0f);
  ImGui::SliderFloat("Point far plane", &s.pointShadowFarPlane, 5.0f, 200.0f);
  ImGui::SliderFloat("Point bias factor", &s.pointShadowPolyFactor, 0.0f,
                     10.0f);
  ImGui::SliderFloat("Point bias units", &s.pointShadowPolyUnits, 0.0f, 20.0f);
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
