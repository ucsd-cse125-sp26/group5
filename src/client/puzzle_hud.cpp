#include "client/puzzle_hud.h"

#include <cstdio>

#include "client/client_game.h"
#include "imgui.h"
#include "shared/components.h"
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

  ImGui::TextUnformatted("Survive the falling cubes!");

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

void drawPuzzleHUDs(const ClientGame& game) {
  auto v = game.renderRegistry.view<shared::FallChallengeState>();
  for (auto e : v) {
    drawFallChallengeHUD(v.get<shared::FallChallengeState>(e));
    break;  // single controller entity
  }
  // Future puzzle HUDs hang off here.
}
