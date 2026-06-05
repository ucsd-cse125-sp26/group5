#include "client/puzzle_hud.h"

#include <cstdio>

#include "client/client_game.h"
#include "imgui.h"
#include "shared/components.h"
#include "shared/puzzles/summer/layout.h"

namespace {

const char* mazeArrowForSlot(uint8_t slot) {
  switch (slot) {
    case 1:
      return "\xe2\x86\x91";  // ↑
    case 2:
      return "\xe2\x86\x93";  // ↓
    case 3:
      return "\xe2\x86\x90";  // ←
    case 4:
      return "\xe2\x86\x92";  // →
    default:
      return "?";
  }
}

void drawWinterMazeHelpHUD(const ClientGame& game) {
  if (!isOverworldMazePuzzleActive(game) ||
      isOverworldMazePuzzleComplete(game)) {
    return;
  }

  constexpr float kPad = 14.0f;
  ImGui::SetNextWindowPos(ImVec2(kPad, kPad), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.78f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.16f, 0.28f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.78f, 1.0f, 0.55f));

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

  if (!ImGui::Begin("##winter_maze_help", nullptr, flags)) {
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return;
  }

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 wmin = ImGui::GetWindowPos();
  const ImVec2 wmax = ImVec2(wmin.x + ImGui::GetWindowWidth(),
                             wmin.y + ImGui::GetWindowHeight());
  dl->AddRect(wmin, wmax, IM_COL32(140, 200, 255, 90), 12.0f, 0, 2.0f);

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.93f, 1.0f, 1.0f));
  ImGui::TextUnformatted("\xe2\x9d\x84 Winter maze");  // ❄
  ImGui::PopStyleColor();
  ImGui::Spacing();

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.98f, 1.0f));
  ImGui::TextUnformatted("One green piece, four directions.");
  ImGui::TextUnformatted("Arrow keys slide it on the board.");
  ImGui::Spacing();
  ImGui::TextUnformatted(
      "P1 \xe2\x86\x91   P2 \xe2\x86\x93   P3 \xe2\x86\x90   P4 \xe2\x86\x92");
  ImGui::TextUnformatted("Guide it to the orange goal.");
  ImGui::Spacing();
  ImGui::TextUnformatted("Q  leave early");

  const uint8_t slot = localOverworldPlayerSlot(game);
  if (slot >= 1 && slot <= 4) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 1.0f, 0.82f, 1.0f));
    ImGui::Text("You: %s", mazeArrowForSlot(slot));
    ImGui::PopStyleColor();
  }
  ImGui::PopStyleColor();

  ImGui::End();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

}  // namespace

static void drawFallChallengeHUD(const shared::FallChallengeState& cs) {
  if (!cs.active) return;

  int slotOf[4];
  float fillOf[4];
  int count = 0;
  for (int i = 0; i < 4; ++i) {
    if (cs.participantMask & (1u << i)) {
      slotOf[count] = i;
      fillOf[count] =
          cs.fill[i] < 0.f ? 0.f : (cs.fill[i] > 1.f ? 1.f : cs.fill[i]);
      ++count;
    }
  }
  if (count == 0) return;

  ImGuiIO& io = ImGui::GetIO();
  const float barW = 600.0f, barH = 44.0f;
  const float winW = barW + 24.0f, winH = barH + 64.0f;

  ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - winW) * 0.5f, 50.0f),
                          ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.65f);
  ImGui::Begin("##fallchallenge", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_NoCollapse);

  ImGui::TextUnformatted("Stay on the platform!");

  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float x0 = p.x, y0 = p.y + 4.0f;
  float segW = barW / static_cast<float>(count);

  for (int s = 0; s < count; ++s) {
    float sx0 = x0 + s * segW;
    float f = fillOf[s];
    ImU32 col =
        f >= 1.0f ? IM_COL32(120, 230, 140, 255) : IM_COL32(70, 160, 100, 255);
    dl->AddRectFilled(ImVec2(sx0, y0), ImVec2(sx0 + segW, y0 + barH),
                      IM_COL32(45, 45, 55, 255));
    dl->AddRectFilled(ImVec2(sx0, y0), ImVec2(sx0 + segW * f, y0 + barH), col);
    if (s > 0)
      dl->AddLine(ImVec2(sx0, y0), ImVec2(sx0, y0 + barH),
                  IM_COL32(0, 0, 0, 255), 2.0f);
    char label[8];
    std::snprintf(label, sizeof(label), "P%d", slotOf[s] + 1);
    dl->AddText(ImVec2(sx0 + 8, y0 + barH * 0.5f - 7),
                IM_COL32(255, 255, 255, 255), label);
  }
  dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + barW, y0 + barH),
              IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);

  ImGui::End();
}

static void drawSummerEscapeHUD(const shared::SummerEscapeState& s,
                                const ClientGame& game) {
  if (!s.active) return;

  const shared::summer::Layout L = shared::summer::Layout::defaults();
  const float mapW = L.mapMaxX - L.mapMinX;
  const float mapH = L.mapMaxY - L.mapMinY;
  if (mapW <= 0.0f || mapH <= 0.0f) return;

  ImGuiIO& io = ImGui::GetIO();
  const float padInner = 220.0f;  // drawable square for the minimap
  const float winW = padInner + 24.0f, winH = padInner + 64.0f;

  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - winW - 20.0f, 20.0f),
                          ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.65f);
  ImGui::Begin("##summerescape", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_NoCollapse);

  char header[48];
  std::snprintf(header, sizeof(header), "Stay in the zone!  Wave %d/%d",
                static_cast<int>(s.wave) + 1,
                shared::summer::Layout::kWaveCount);
  ImGui::TextUnformatted(header);

  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float ox = p.x, oy = p.y + 4.0f;

  // World (x,y) -> overlay pixel. Screen Y is flipped so world +Y is "up".
  auto toScreen = [&](float wx, float wy) -> ImVec2 {
    float u = (wx - L.mapMinX) / mapW;
    float v = (wy - L.mapMinY) / mapH;
    if (u < 0.0f)
      u = 0.0f;
    else if (u > 1.0f)
      u = 1.0f;
    if (v < 0.0f)
      v = 0.0f;
    else if (v > 1.0f)
      v = 1.0f;
    return {ox + u * padInner, oy + (1.0f - v) * padInner};
  };

  // Map background + border.
  dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + padInner, oy + padInner),
                    IM_COL32(28, 30, 38, 255));
  dl->AddRect(ImVec2(ox, oy), ImVec2(ox + padInner, oy + padInner),
              IM_COL32(255, 255, 255, 160), 0.0f, 0, 1.5f);

  // Next-wave target region (faint dashed-ish outline).
  {
    ImVec2 a = toScreen(s.tgtMinX, s.tgtMaxY);
    ImVec2 b = toScreen(s.tgtMaxX, s.tgtMinY);
    dl->AddRect(a, b, IM_COL32(255, 200, 80, 120), 0.0f, 0, 1.5f);
  }

  // Live survival region (red box).
  {
    ImVec2 a = toScreen(s.regMinX, s.regMaxY);
    ImVec2 b = toScreen(s.regMaxX, s.regMinY);
    dl->AddRectFilled(a, b, IM_COL32(220, 60, 60, 45));
    dl->AddRect(a, b, IM_COL32(240, 70, 70, 255), 0.0f, 0, 2.5f);
  }

  // Player dots (read replicated Position + RenderInfo.playerSlot).
  static const ImU32 kSlotColors[4] = {
      IM_COL32(90, 170, 255, 255), IM_COL32(120, 230, 140, 255),
      IM_COL32(255, 210, 90, 255), IM_COL32(220, 130, 255, 255)};
  auto players =
      game.renderRegistry.view<shared::Position, shared::RenderInfo>();
  for (auto e : players) {
    const auto& ri = players.get<shared::RenderInfo>(e);
    if (ri.playerSlot < 1 || ri.playerSlot > 4) continue;
    const auto& pos = players.get<shared::Position>(e);
    ImVec2 c = toScreen(pos.x, pos.y);
    const ImU32 col = kSlotColors[ri.playerSlot - 1];
    dl->AddCircleFilled(c, 4.5f, col);
    dl->AddCircle(c, 4.5f, IM_COL32(0, 0, 0, 200), 0, 1.5f);
    char lbl[4];
    std::snprintf(lbl, sizeof(lbl), "%d", static_cast<int>(ri.playerSlot));
    dl->AddText(ImVec2(c.x + 5.0f, c.y - 7.0f), col, lbl);
  }

  ImGui::End();
}

void drawPuzzleHUDs(const ClientGame& game) {
  drawWinterMazeHelpHUD(game);

  auto v = game.renderRegistry.view<shared::FallChallengeState>();
  for (auto e : v) {
    drawFallChallengeHUD(v.get<shared::FallChallengeState>(e));
    break;  // single controller entity
  }
  auto se = game.renderRegistry.view<shared::SummerEscapeState>();
  for (auto e : se) {
    drawSummerEscapeHUD(se.get<shared::SummerEscapeState>(e), game);
    break;  // single controller entity
  }
}
