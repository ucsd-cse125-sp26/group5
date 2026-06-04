#include "client/debug_panel.h"

#include "client/client_game.h"
#include "client/client_graphics.h"
#include "imgui.h"
#include "shared/protocol.h"

namespace {

// Season/puzzle order matches shared::SectionSeasonMap (WINTER=0, FALL=1,
// SUMMER=2, SPRING=3). Index = arg sent to the server.
const char* kSeasonName[4] = {"Winter", "Fall", "Summer", "Spring"};

// Per-season button tints so the four puzzle rows read at a glance.
const ImVec4 kSeasonColor[4] = {
    ImVec4(0.30f, 0.45f, 0.65f, 1.0f),  // Winter  – icy blue
    ImVec4(0.62f, 0.38f, 0.20f, 1.0f),  // Fall    – amber
    ImVec4(0.60f, 0.52f, 0.18f, 1.0f),  // Summer  – gold
    ImVec4(0.30f, 0.55f, 0.32f, 1.0f),  // Spring  – green
};

void pushCmd(ClientGame& game, shared::DebugCommand cmd, uint32_t arg = 0) {
  shared::DebugCommandPacket pkt;
  pkt.cmd = cmd;
  pkt.arg = arg;
  game.debugQueue.tryPush(pkt);  // drop-if-full is fine for human-paced clicks
}

// Full-width colored button helper.
bool bigButton(const char* label, const ImVec2& size, const ImVec4* tint) {
  if (tint) ImGui::PushStyleColor(ImGuiCol_Button, *tint);
  bool clicked = ImGui::Button(label, size);
  if (tint) ImGui::PopStyleColor();
  return clicked;
}

}  // namespace

void drawDebugPanel(Graphics& g, ClientGame& game, bool& open) {
  if (!open) return;

  ImGui::SetNextWindowSize(ImVec2(760.0f, 0.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.92f);
  if (!ImGui::Begin("DEBUG CONTROL PANEL  (Ctrl+Shift+\\)", &open)) {
    ImGui::End();
    return;
  }

  const ImVec2 kBtn(232.0f, 52.0f);  // large two-per-row buttons
  const float kCellW = 116.0f;       // per-puzzle action buttons
  const ImVec2 kCell(kCellW, 46.0f);

  // ── Seasons ──────────────────────────────────────────────
  ImGui::SeparatorText("SEASON");
  for (int s = 0; s < 4; ++s) {
    if (bigButton(kSeasonName[s], kBtn, &kSeasonColor[s])) {
      pushCmd(game, shared::DebugCommand::SET_SEASON, static_cast<uint32_t>(s));
    }
    if (s % 2 == 0) ImGui::SameLine();
  }
  if (ImGui::Button("Cycle Season", kBtn)) {
    pushCmd(game, shared::DebugCommand::CYCLE_SEASON);
  }

  // ── Fragments ────────────────────────────────────────────
  ImGui::SeparatorText("FRAGMENTS");
  if (ImGui::Button("Spawn Current Fragment", kBtn)) {
    pushCmd(game, shared::DebugCommand::SPAWN_FRAGMENT_CURRENT);
  }
  ImGui::SameLine();
  if (ImGui::Button("Spawn ALL Fragments", kBtn)) {
    pushCmd(game, shared::DebugCommand::SPAWN_FRAGMENT_ALL);
  }

  // ── Puzzles: per-season Teleport / Start / Finish / Pickup ────
  // Finish = pretend-win the minigame (reveals the fragment). Pickup Fragment =
  // organically collect it (completes the section, advances the season).
  ImGui::SeparatorText("PUZZLES");
  for (int s = 0; s < 4; ++s) {
    ImGui::PushID(s);
    ImGui::PushStyleColor(ImGuiCol_Text, kSeasonColor[s]);
    ImGui::TextUnformatted(kSeasonName[s]);
    ImGui::PopStyleColor();
    ImGui::SameLine(110.0f);
    if (ImGui::Button("Teleport", kCell)) {
      pushCmd(game, shared::DebugCommand::TELEPORT_TO_PUZZLE,
              static_cast<uint32_t>(s));
    }
    ImGui::SameLine();
    if (ImGui::Button("Start", kCell)) {
      pushCmd(game, shared::DebugCommand::START_PUZZLE,
              static_cast<uint32_t>(s));
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.55f, 0.20f, 1.0f));
    if (ImGui::Button("Finish", kCell)) {
      pushCmd(game, shared::DebugCommand::FINISH_PUZZLE,
              static_cast<uint32_t>(s));
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.45f, 1.0f));
    if (ImGui::Button("Pickup", kCell)) {
      pushCmd(game, shared::DebugCommand::PICKUP_FRAGMENT,
              static_cast<uint32_t>(s));
    }
    ImGui::PopStyleColor();
    ImGui::PopID();
  }

  // ── World / flow ─────────────────────────────────────────
  ImGui::SeparatorText("WORLD");
  if (ImGui::Button("Toggle Barrier Collision", kBtn)) {
    pushCmd(game, shared::DebugCommand::TOGGLE_BARRIER_COLLISION);
  }
  ImGui::SameLine();
  if (ImGui::Button("Toggle Barrier Visibility", kBtn)) {
    pushCmd(game, shared::DebugCommand::TOGGLE_BARRIER_VISIBILITY);
  }
  if (ImGui::Button("Reset Players to Spawn", kBtn)) {
    pushCmd(game, shared::DebugCommand::RESET_TO_OVERWORLD_SPAWN);
  }
  ImGui::SameLine();
  if (ImGui::Button("Print Positions", kBtn)) {
    pushCmd(game, shared::DebugCommand::PRINT_POSITIONS);
  }
  if (bigButton("TRIGGER CREDITS  (once per run)", ImVec2(-1.0f, 52.0f),
                &kSeasonColor[1])) {
    pushCmd(game, shared::DebugCommand::TRIGGER_CREDITS);
  }

  // ── Graphics (local only — no server round-trip) ─────────
  ImGui::SeparatorText("GRAPHICS (local)");
  if (ImGui::Button("Cycle Debug Channel", kBtn)) g.cycleDebugChannel();
  ImGui::SameLine();
  if (ImGui::Button("Reload Shaders", kBtn)) g.reloadShaders();
  if (ImGui::Button("Toggle Fullscreen", kBtn)) g.toggleFullscreen();
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Debug channel: %d", static_cast<int>(g.debugChannel));

  ImGui::End();
}
