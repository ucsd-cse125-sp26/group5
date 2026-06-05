#include "client/debug_panel.h"

#include "client/client_game.h"
#include "client/client_graphics.h"
#include "imgui.h"
#include "shared/components.h"
#include "shared/log.h"
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

void pushCmd(ClientGame& game, shared::DebugCommand cmd, uint32_t arg = 0,
             uint32_t arg2 = 0, float farg = 0.0f) {
  shared::DebugCommandPacket pkt;
  pkt.cmd = cmd;
  pkt.arg = arg;
  pkt.arg2 = arg2;
  pkt.farg = farg;
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

  // ── Winter maze: per-player directional power ────────────
  // Each player normally drives only ONE arrow direction (slot 1=Up, 2=Down,
  // 3=Left, 4=Right). "All" removes the restriction so that player can drive
  // every direction — use it to unstick the maze when a slot is missing.
  ImGui::SeparatorText("MAZE POWERS  (Winter — per player)");
  {
    const ImVec2 kSmall(76.0f, 38.0f);
    const char* kDirName[5] = {"All", "Up", "Down", "Left", "Right"};
    for (int slot = 1; slot <= 4; ++slot) {
      ImGui::PushID(1000 + slot);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("P%d", slot);
      ImGui::SameLine(48.0f);
      for (int d = 0; d < 5; ++d) {
        // d maps directly to shared::MazeDirection (0=NONE/All ... 4=RIGHT).
        if (ImGui::Button(kDirName[d], kSmall)) {
          pushCmd(game, shared::DebugCommand::SET_MAZE_POWER,
                  static_cast<uint32_t>(slot), static_cast<uint32_t>(d));
        }
        if (d < 4) ImGui::SameLine();
      }
      ImGui::PopID();
    }
  }

  // ── Spring tangram: role-isolation stage ─────────────────
  // Lower stages hand more abilities to more players; stage 0 lets everyone do
  // everything (push / rotate / see color / see slots). Set the puzzle running
  // first, then dial the stage down to unstick it.
  ImGui::SeparatorText("TANGRAM ROLES  (Spring — isolation stage)");
  {
    struct Stage {
      const char* label;
      uint32_t value;
    };
    static const Stage kStages[6] = {{"All (0)", 0},    {"Infra (1)", 1},
                                     {"P4 Rot (2)", 2}, {"P3 Slot (3)", 3},
                                     {"P2 Col (4)", 4}, {"Full (5)", 5}};
    const ImVec2 kStageBtn(120.0f, 40.0f);
    for (int i = 0; i < 6; ++i) {
      if (ImGui::Button(kStages[i].label, kStageBtn)) {
        pushCmd(game, shared::DebugCommand::SET_TANGRAM_STAGE,
                kStages[i].value);
      }
      if (i % 3 != 2 && i < 5) ImGui::SameLine();
    }
  }

  // ── Spring tangram: per-player ability grants ────────────
  // Hand one ability to one player on TOP of the stage rules (e.g. let P1 also
  // rotate, or give a missing role's power to someone present). Checkboxes
  // mirror the live replicated grant state.
  ImGui::SeparatorText("TANGRAM GRANTS  (Spring — per player)");
  {
    uint8_t masks[4] = {0, 0, 0, 0};  // [ability] -> per-slot bitmask
    bool active = false;
    auto view = game.renderRegistry.view<shared::OverworldTangramPuzzleState>();
    for (auto ent : view) {
      const auto& st = view.get<shared::OverworldTangramPuzzleState>(ent);
      if (!st.active) continue;
      active = true;
      masks[0] = st.grantPush;
      masks[1] = st.grantRotate;
      masks[2] = st.grantColor;
      masks[3] = st.grantSlots;
      break;
    }
    if (!active) {
      ImGui::TextDisabled("(start the tangram puzzle to grant powers)");
    }
    const char* kAbil[4] = {"Push", "Rotate", "Color", "Slots"};
    for (int slot = 1; slot <= 4; ++slot) {
      ImGui::PushID(3000 + slot);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("P%d", slot);
      ImGui::SameLine(48.0f);
      const uint8_t bit = static_cast<uint8_t>(1u << (slot - 1));
      for (int a = 0; a < 4; ++a) {
        bool on = (masks[a] & bit) != 0;
        ImGui::PushID(a);
        if (ImGui::Checkbox(kAbil[a], &on)) {
          pushCmd(game, shared::DebugCommand::SET_TANGRAM_GRANT,
                  static_cast<uint32_t>(slot), static_cast<uint32_t>(a),
                  on ? 1.0f : 0.0f);
        }
        ImGui::PopID();
        if (a < 3) ImGui::SameLine();
      }
      ImGui::PopID();
    }
  }

  // ── Teleport an individual player ────────────────────────
  ImGui::SeparatorText("TELEPORT PLAYER  (per player)");
  {
    const char* kDestName[5] = {"Winter", "Fall", "Summer", "Spring", "Spawn"};
    static int sel[5] = {0, 0, 0, 0, 0};  // index by slot (1..4)
    const ImVec2 kGo(96.0f, 38.0f);
    for (int slot = 1; slot <= 4; ++slot) {
      ImGui::PushID(2000 + slot);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("P%d", slot);
      ImGui::SameLine(48.0f);
      ImGui::SetNextItemWidth(160.0f);
      ImGui::Combo("##dest", &sel[slot], kDestName, 5);
      ImGui::SameLine();
      if (ImGui::Button("Teleport", kGo)) {
        pushCmd(game, shared::DebugCommand::TELEPORT_PLAYER,
                static_cast<uint32_t>(slot), static_cast<uint32_t>(sel[slot]));
      }
      ImGui::PopID();
    }
  }

  // ── Fall challenge difficulty (live) ─────────────────────
  ImGui::SeparatorText("FALL DIFFICULTY  (live)");
  {
    static float fillRate = 0.05f;       // FallChallengeState default
    static float hitPenalty = 0.25f;     // FallChallengeState default
    static float spawnInterval = 0.12f;  // spawned-zone interval (game_state)
    static int burstsToSwitch = 8;       // FallingHazardZone default
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::SliderFloat("Fill rate (/s)", &fillRate, 0.01f, 0.30f, "%.3f")) {
      pushCmd(game, shared::DebugCommand::SET_FALL_PARAM,
              static_cast<uint32_t>(shared::DebugFallParam::FILL_RATE), 0,
              fillRate);
    }
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::SliderFloat("Hit penalty", &hitPenalty, 0.0f, 0.60f, "%.3f")) {
      pushCmd(game, shared::DebugCommand::SET_FALL_PARAM,
              static_cast<uint32_t>(shared::DebugFallParam::HIT_PENALTY), 0,
              hitPenalty);
    }
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::SliderFloat("Spawn interval (s)", &spawnInterval, 0.03f, 1.0f,
                           "%.3f")) {
      pushCmd(game, shared::DebugCommand::SET_FALL_PARAM,
              static_cast<uint32_t>(shared::DebugFallParam::SPAWN_INTERVAL), 0,
              spawnInterval);
    }
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::SliderInt("Bursts/pattern", &burstsToSwitch, 1, 30)) {
      pushCmd(game, shared::DebugCommand::SET_FALL_PARAM,
              static_cast<uint32_t>(shared::DebugFallParam::BURSTS_TO_SWITCH),
              0, static_cast<float>(burstsToSwitch));
    }
  }

  // ── Summer escape difficulty (applies to current/next wave) ──
  ImGui::SeparatorText("SUMMER DIFFICULTY  (live)");
  {
    static float shrinkFactor = 0.5f;   // summer::Layout default
    static float waveDuration = 10.0f;  // summer::Layout default
    static float startGrace = 0.75f;    // summer::Layout default
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::SliderFloat("Shrink factor", &shrinkFactor, 0.2f, 0.95f,
                           "%.2f")) {
      pushCmd(game, shared::DebugCommand::SET_SUMMER_PARAM,
              static_cast<uint32_t>(shared::DebugSummerParam::SHRINK_FACTOR), 0,
              shrinkFactor);
    }
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::SliderFloat("Wave duration (s)", &waveDuration, 3.0f, 30.0f,
                           "%.1f")) {
      pushCmd(game, shared::DebugCommand::SET_SUMMER_PARAM,
              static_cast<uint32_t>(shared::DebugSummerParam::WAVE_DURATION), 0,
              waveDuration);
    }
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::SliderFloat("Start grace (s)", &startGrace, 0.0f, 5.0f,
                           "%.2f")) {
      pushCmd(game, shared::DebugCommand::SET_SUMMER_PARAM,
              static_cast<uint32_t>(shared::DebugSummerParam::START_GRACE), 0,
              startGrace);
    }
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
  if (bigButton("ROLL / RE-ROLL CREDITS", ImVec2(-1.0f, 52.0f),
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

  // ── Logging ──────────────────────────────────────────────
  ImGui::SeparatorText("LOGGING");
  ImGui::Checkbox("Client debug log output", &shared::log::debugEnabled);
  if (ImGui::Button("Toggle Server Debug Log", kBtn)) {
    pushCmd(game, shared::DebugCommand::TOGGLE_DEBUG_LOG);
  }

  ImGui::End();
}
