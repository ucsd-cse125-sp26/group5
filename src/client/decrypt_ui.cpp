#include "client/decrypt_ui.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

// clang-format off
#include <glad/gl.h>
// clang-format on

#include "client/client_game.h"
#include "imgui.h"
#include "shared/protocol.h"
#include "shared/util.h"

namespace decrypt_ui {
namespace {

GLuint boardTexture = 0;
int boardTexW = 0;
int boardTexH = 0;
bool assetsLoaded = false;
char answerBuf[128] = {};
bool showWrongMessage = false;
bool pendingFocus = false;

void loadBoardTexture() {
  if (boardTexture != 0) return;
  const std::filesystem::path path =
      exeDir() / "assets/decrypt/decrypt-screen.png";
  int w = 0;
  int h = 0;
  int ch = 0;
  stbi_set_flip_vertically_on_load(false);
  unsigned char* pixels = stbi_load(path.string().c_str(), &w, &h, &ch, 4);
  if (!pixels || w <= 0 || h <= 0) {
    fprintf(stderr, "[DecryptUI] failed to load %s\n", path.string().c_str());
    if (pixels) stbi_image_free(pixels);
    return;
  }
  glGenTextures(1, &boardTexture);
  glBindTexture(GL_TEXTURE_2D, boardTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  stbi_image_free(pixels);
  boardTexW = w;
  boardTexH = h;
  assetsLoaded = true;
}

void submitAnswer(ClientGame& game) {
  if (answerBuf[0] == '\0') return;
  shared::DecryptSubmitPacket pkt;
  pkt.text[sizeof(pkt.text) - 1] = '\0';
  std::strncpy(pkt.text, answerBuf, sizeof(pkt.text) - 1);
  game.decryptSubmitQueue.tryPush(pkt);
  showWrongMessage = false;
}

}  // namespace

void onDecryptActivated() { pendingFocus = true; }

void ensureAssetsLoaded(Graphics& graphics) {
  (void)graphics;
  loadBoardTexture();
}

void drawDecryptOverlay(Graphics& graphics, ClientGame& game) {
  (void)graphics;
  if (game.currentGameState != shared::GameStateType::DECRYPT) return;
  if (!ImGui::GetCurrentContext()) return;

  ensureAssetsLoaded(graphics);
  showWrongMessage = game.decryptWrongAnswer.load(std::memory_order_acquire);

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (!ImGui::Begin("##DecryptOverlay", nullptr, flags)) {
    ImGui::End();
    return;
  }

  if (boardTexture != 0 && boardTexW > 0 && boardTexH > 0) {
    const float maxW = io.DisplaySize.x * 0.92f;
    const float maxH = io.DisplaySize.y * 0.72f;
    const float aspect =
        static_cast<float>(boardTexW) / static_cast<float>(boardTexH);
    float drawW = maxW;
    float drawH = drawW / aspect;
    if (drawH > maxH) {
      drawH = maxH;
      drawW = drawH * aspect;
    }
    const float x = (io.DisplaySize.x - drawW) * 0.5f;
    const float y = io.DisplaySize.y * 0.06f;
    ImGui::SetCursorPos(ImVec2(x, y));
    ImGui::Image((ImTextureID)(uintptr_t)boardTexture, ImVec2(drawW, drawH));
  } else {
    ImGui::SetWindowFontScale(1.6f);
    ImGui::SetCursorPos(
        ImVec2(io.DisplaySize.x * 0.5f - 180.0f, io.DisplaySize.y * 0.25f));
    ImGui::TextUnformatted("Decode the message with the following key!");
  }

  const float inputW = std::min(io.DisplaySize.x * 0.75f, 720.0f);
  const float inputH = 42.0f;
  ImGui::SetCursorPos(
      ImVec2((io.DisplaySize.x - inputW) * 0.5f, io.DisplaySize.y - 72.0f));
  if (pendingFocus) {
    ImGui::SetKeyboardFocusHere();
    pendingFocus = false;
  }
  ImGui::PushItemWidth(inputW);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));
  ImGui::SetNextItemWidth(inputW);
  const ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
  bool submitted = false;
  if (ImGui::InputTextWithHint("##decrypt_answer", "Type the decoded message...",
                               answerBuf, sizeof(answerBuf), inputFlags)) {
    submitted = true;
  }
  if (ImGui::IsItemActive() &&
      (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
       ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
    submitted = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Submit", ImVec2(80.0f, 0.0f))) {
    submitted = true;
  }
  ImGui::PopStyleVar();
  ImGui::PopItemWidth();

  if (submitted) submitAnswer(game);

  if (showWrongMessage) {
    ImGui::SetWindowFontScale(1.1f);
    const char* msg = "Incorrect — try again";
    const float msgW = ImGui::CalcTextSize(msg).x;
    ImGui::SetCursorPos(
        ImVec2((io.DisplaySize.x - msgW) * 0.5f, io.DisplaySize.y - 110.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
    ImGui::TextUnformatted(msg);
    ImGui::PopStyleColor();
  }

  ImGui::End();
}

}  // namespace decrypt_ui
