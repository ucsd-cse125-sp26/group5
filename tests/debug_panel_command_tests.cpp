// End-to-end coverage for the demo "unstick" debug-panel commands: build a
// DebugCommandPacket exactly as the client panel does, push it onto
// ServerGame::pendingDebugCommands, run server_debug::processPendingCommands
// (the real game-thread drain), and assert the resulting game state. network is
// null here, so handlers that broadcast are exercised on their no-network path.

#include <gtest/gtest.h>

#include <cstdint>

#include "server/game/puzzles/tangram/puzzle.h"
#include "server/server_debug.h"
#include "server/server_game.h"
#include "shared/components.h"
#include "shared/protocol.h"
#include "shared/puzzles/summer/layout.h"
#include "shared/puzzles/tangram/roles.h"

namespace {

// Fake peer keys for active_players — handlers only use them as map keys, never
// dereference them, so any distinct non-null pointer works.
ENetPeer* fakePeer(uintptr_t i) { return reinterpret_cast<ENetPeer*>(i); }

// Overworld avatar with Position + slot, registered in active_players so the
// per-player teleport / maze-power handlers can find it. No PhysicsBody: the
// teleport handler guards on PhysicsBody so the position update still applies.
entt::entity makePlayer(ServerGame& game, uint8_t slot, glm::vec3 pos) {
  auto ent = game.registry.create();
  game.registry.emplace<shared::Entity>(ent, game.nextEntityId++);
  game.registry.emplace<shared::OverworldTag>(ent);
  game.registry.emplace<shared::Position>(ent, pos.x, pos.y, pos.z);
  game.registry.emplace<shared::Velocity>(ent, 0.0f, 0.0f, 0.0f);
  game.registry.emplace<shared::RenderInfo>(ent, "cube", 1.0f, 1.0f, 1.0f);
  game.registry.get<shared::RenderInfo>(ent).playerSlot = slot;

  PlayerAvatars avatars{};
  avatars.overworld_avatar = ent;
  avatars.maze_avatar = entt::null;
  game.active_players[fakePeer(slot)] = avatars;
  return ent;
}

void runCmd(ServerGame& game, shared::DebugCommand cmd, uint32_t arg = 0,
            uint32_t arg2 = 0, float farg = 0.0f) {
  shared::DebugCommandPacket pkt;
  pkt.cmd = cmd;
  pkt.arg = arg;
  pkt.arg2 = arg2;
  pkt.farg = farg;
  game.pendingDebugCommands.push_back(pkt);
  server_debug::processPendingCommands(game);
}

}  // namespace

// ── Winter maze powers ─────────────────────────────────────────────────────
TEST(DebugPanelCommands, SetMazePowerBindsSingleDirection) {
  ServerGame game;
  auto p = makePlayer(game, 1, {0, 0, 0});

  runCmd(game, shared::DebugCommand::SET_MAZE_POWER, /*slot=*/1,
         /*dir=*/static_cast<uint32_t>(shared::MazeDirection::RIGHT));

  ASSERT_TRUE(game.registry.all_of<shared::MazePadBinding>(p));
  EXPECT_EQ(game.registry.get<shared::MazePadBinding>(p).pad,
            shared::MazeDirection::RIGHT);
}

TEST(DebugPanelCommands, SetMazePowerAllRemovesBinding) {
  ServerGame game;
  auto p = makePlayer(game, 2, {0, 0, 0});
  game.registry.emplace<shared::MazePadBinding>(p, shared::MazeDirection::DOWN);

  // arg2 == 0 (NONE) means "grant all arrows" -> binding removed.
  runCmd(game, shared::DebugCommand::SET_MAZE_POWER, /*slot=*/2, /*dir=*/0);

  EXPECT_FALSE(game.registry.all_of<shared::MazePadBinding>(p));
}

TEST(DebugPanelCommands, SetMazePowerOnlyAffectsTargetSlot) {
  ServerGame game;
  auto p1 = makePlayer(game, 1, {0, 0, 0});
  auto p3 = makePlayer(game, 3, {0, 0, 0});

  runCmd(game, shared::DebugCommand::SET_MAZE_POWER, /*slot=*/3,
         static_cast<uint32_t>(shared::MazeDirection::UP));

  EXPECT_FALSE(game.registry.all_of<shared::MazePadBinding>(p1));
  ASSERT_TRUE(game.registry.all_of<shared::MazePadBinding>(p3));
  EXPECT_EQ(game.registry.get<shared::MazePadBinding>(p3).pad,
            shared::MazeDirection::UP);
}

// ── Spring tangram role-isolation stage ────────────────────────────────────
TEST(DebugPanelCommands, SetTangramStageUpdatesLiveState) {
  ServerGame game;
  game.overworldTangramActive = true;
  game.overworldTangramController = game.registry.create();
  game.registry.emplace<shared::OverworldTangramPuzzleState>(
      game.overworldTangramController);
  auto& st = game.registry.get<shared::OverworldTangramPuzzleState>(
      game.overworldTangramController);
  st.active = true;
  st.roleIsolationStage = shared::tangram_roles::kStagePushP1;

  runCmd(game, shared::DebugCommand::SET_TANGRAM_STAGE, /*stage=*/0);
  EXPECT_EQ(st.roleIsolationStage, 0);

  runCmd(game, shared::DebugCommand::SET_TANGRAM_STAGE, /*stage=*/3);
  EXPECT_EQ(st.roleIsolationStage, 3);
}

TEST(DebugPanelCommands, SetTangramStageClampsAboveMax) {
  ServerGame game;
  game.overworldTangramActive = true;
  game.overworldTangramController = game.registry.create();
  game.registry.emplace<shared::OverworldTangramPuzzleState>(
      game.overworldTangramController);
  auto& st = game.registry.get<shared::OverworldTangramPuzzleState>(
      game.overworldTangramController);
  st.active = true;

  runCmd(game, shared::DebugCommand::SET_TANGRAM_STAGE, /*stage=*/99);
  EXPECT_EQ(st.roleIsolationStage, shared::tangram_roles::kStagePushP1);
}

// ── Spring tangram per-player grants ───────────────────────────────────────
TEST(DebugPanelCommands, SetTangramGrantTogglesMaskAndOverridesStage) {
  ServerGame game;
  game.overworldTangramActive = true;
  game.overworldTangramController = game.registry.create();
  game.registry.emplace<shared::OverworldTangramPuzzleState>(
      game.overworldTangramController);
  auto& st = game.registry.get<shared::OverworldTangramPuzzleState>(
      game.overworldTangramController);
  st.active = true;
  st.roleIsolationStage = shared::tangram_roles::kStagePushP1;  // full split

  // At stage 5 only slot 4 may rotate; grant rotate to slot 1.
  ASSERT_FALSE(shared::tangram_roles::canRotate(st.roleIsolationStage, 1));
  runCmd(game, shared::DebugCommand::SET_TANGRAM_GRANT, /*slot=*/1,
         static_cast<uint32_t>(shared::DebugTangramAbility::ROTATE),
         /*enable=*/1.0f);
  EXPECT_EQ(st.grantRotate, 0b0001);
  EXPECT_TRUE(shared::tangram_roles::canRotate(st.roleIsolationStage, 1,
                                               st.grantRotate));

  // Revoking clears just that slot's bit.
  runCmd(game, shared::DebugCommand::SET_TANGRAM_GRANT, /*slot=*/1,
         static_cast<uint32_t>(shared::DebugTangramAbility::ROTATE),
         /*enable=*/0.0f);
  EXPECT_EQ(st.grantRotate, 0);
  EXPECT_FALSE(shared::tangram_roles::canRotate(st.roleIsolationStage, 1,
                                                st.grantRotate));
}

TEST(DebugPanelCommands, SetTangramGrantPerAbilityIndependent) {
  ServerGame game;
  game.overworldTangramActive = true;
  game.overworldTangramController = game.registry.create();
  game.registry.emplace<shared::OverworldTangramPuzzleState>(
      game.overworldTangramController);
  auto& st = game.registry.get<shared::OverworldTangramPuzzleState>(
      game.overworldTangramController);
  st.active = true;

  runCmd(game, shared::DebugCommand::SET_TANGRAM_GRANT, /*slot=*/2,
         static_cast<uint32_t>(shared::DebugTangramAbility::COLOR), 1.0f);
  runCmd(game, shared::DebugCommand::SET_TANGRAM_GRANT, /*slot=*/3,
         static_cast<uint32_t>(shared::DebugTangramAbility::SLOTS), 1.0f);
  runCmd(game, shared::DebugCommand::SET_TANGRAM_GRANT, /*slot=*/4,
         static_cast<uint32_t>(shared::DebugTangramAbility::PUSH), 1.0f);

  EXPECT_EQ(st.grantColor, 0b0010);  // slot 2
  EXPECT_EQ(st.grantSlots, 0b0100);  // slot 3
  EXPECT_EQ(st.grantPush, 0b1000);   // slot 4
  EXPECT_EQ(st.grantRotate, 0);      // untouched
}

// ── Per-player teleport ────────────────────────────────────────────────────
TEST(DebugPanelCommands, TeleportPlayerMovesOnlyTargetSlot) {
  ServerGame game;
  auto p1 = makePlayer(game, 1, {5.0f, 5.0f, 5.0f});
  auto p2 = makePlayer(game, 2, {5.0f, 5.0f, 5.0f});

  runCmd(game, shared::DebugCommand::TELEPORT_PLAYER, /*slot=*/2,
         static_cast<uint32_t>(shared::DebugTeleportDest::WINTER_PUZZLE));

  // Expected winter target mirrors server_debug::puzzleSlotTarget(WINTER, 2):
  // backed off -Y outside the maze trigger, slot-2 lateral offset = -1.0.
  const auto& L = game.mazeLayout;
  const float backoff = L.halfExtent + 4.0f;
  const auto& tp = game.registry.get<shared::Position>(p2);
  EXPECT_NEAR(tp.x, L.triggerCenterX - 1.0f, 0.001f);
  EXPECT_NEAR(tp.y, L.triggerCenterY - backoff, 0.001f);
  EXPECT_NEAR(tp.z, L.triggerCenterZ, 0.001f);

  // Slot 1 must be untouched.
  const auto& up = game.registry.get<shared::Position>(p1);
  EXPECT_NEAR(up.x, 5.0f, 0.001f);
  EXPECT_NEAR(up.y, 5.0f, 0.001f);
  EXPECT_NEAR(up.z, 5.0f, 0.001f);
}

// ── Fall challenge difficulty ──────────────────────────────────────────────
TEST(DebugPanelCommands, SetFallParamTunesStateAndZone) {
  ServerGame game;
  auto stateEnt = game.registry.create();
  game.registry.emplace<shared::FallChallengeState>(stateEnt);
  auto zoneEnt = game.registry.create();
  game.registry.emplace<shared::FallingHazardZone>(zoneEnt);

  runCmd(game, shared::DebugCommand::SET_FALL_PARAM,
         static_cast<uint32_t>(shared::DebugFallParam::FILL_RATE), 0, 0.2f);
  runCmd(game, shared::DebugCommand::SET_FALL_PARAM,
         static_cast<uint32_t>(shared::DebugFallParam::HIT_PENALTY), 0, 0.1f);
  runCmd(game, shared::DebugCommand::SET_FALL_PARAM,
         static_cast<uint32_t>(shared::DebugFallParam::SPAWN_INTERVAL), 0,
         0.05f);
  runCmd(game, shared::DebugCommand::SET_FALL_PARAM,
         static_cast<uint32_t>(shared::DebugFallParam::BURSTS_TO_SWITCH), 0,
         3.0f);

  const auto& cs = game.registry.get<shared::FallChallengeState>(stateEnt);
  EXPECT_NEAR(cs.fillRate, 0.2f, 0.0001f);
  EXPECT_NEAR(cs.hitPenalty, 0.1f, 0.0001f);
  const auto& z = game.registry.get<shared::FallingHazardZone>(zoneEnt);
  EXPECT_NEAR(z.interval, 0.05f, 0.0001f);
  EXPECT_EQ(z.burstsUntilSwitch, 3);
}

// ── Summer escape difficulty ───────────────────────────────────────────────
TEST(DebugPanelCommands, SetSummerParamTunesLayout) {
  ServerGame game;

  runCmd(game, shared::DebugCommand::SET_SUMMER_PARAM,
         static_cast<uint32_t>(shared::DebugSummerParam::SHRINK_FACTOR), 0,
         0.8f);
  runCmd(game, shared::DebugCommand::SET_SUMMER_PARAM,
         static_cast<uint32_t>(shared::DebugSummerParam::WAVE_DURATION), 0,
         15.0f);
  runCmd(game, shared::DebugCommand::SET_SUMMER_PARAM,
         static_cast<uint32_t>(shared::DebugSummerParam::START_GRACE), 0, 2.0f);

  EXPECT_NEAR(game.summerLayout.shrinkFactor, 0.8f, 0.0001f);
  for (int i = 0; i < shared::summer::Layout::kWaveCount; ++i) {
    EXPECT_NEAR(game.summerLayout.waveDurationSec[i], 15.0f, 0.0001f);
  }
  EXPECT_NEAR(game.summerLayout.startGraceSec, 2.0f, 0.0001f);
}

// ── Credits re-roll: latch no longer blocks repeat triggers ────────────────
TEST(DebugPanelCommands, TriggerCreditsForcesEvenAfterRolled) {
  ServerGame game;
  game.creditsRolled = true;  // pretend credits already played this run

  // Pre-fix this would early-return; now it re-rolls. network is null so the
  // broadcast is skipped, but the call must complete and keep the latch set.
  runCmd(game, shared::DebugCommand::TRIGGER_CREDITS);
  EXPECT_TRUE(game.creditsRolled);
}
