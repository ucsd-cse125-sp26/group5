#include "server/game/maze.h"

#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

#include "server/game/maze_spirit_control.h"
#include "server/server_game.h"
#include "server/server_network.h"
#include "shared/components.h"
#include "shared/input.h"
#include "shared/net/packet_utils.h"

namespace {

entt::entity findEntityByNumericId(const entt::registry& reg,
                                   uint32_t numericId) {
  auto view = reg.view<shared::Entity>();
  for (auto e : view) {
    if (view.get<shared::Entity>(e).id == numericId) return e;
  }
  return entt::null;
}

entt::entity findWinterSection(const entt::registry& reg) {
  auto view = reg.view<shared::SectionController>();
  for (auto e : view) {
    const auto& sc = view.get<shared::SectionController>(e);
    if (sc.type == shared::SectionSeasonMap::WINTER && sc.unlocked) return e;
  }
  return entt::null;
}

bool mazePuzzleAllowsSpiritMove(const ServerGame& game) {
  entt::entity winter = findWinterSection(game.registry);
  if (winter == entt::null) return false;
  const uint32_t puzzleId =
      game.registry.get<shared::SectionController>(winter).puzzleID;
  entt::entity puzzleEnt = findEntityByNumericId(game.registry, puzzleId);
  if (puzzleEnt == entt::null ||
      !game.registry.all_of<shared::PuzzleComponent>(puzzleEnt))
    return false;
  return game.registry.get<shared::PuzzleComponent>(puzzleEnt).phase ==
         shared::RunPhase::INPROGRESS;
}

void mazeDirToDelta(shared::MazeDirection d, int8_t& dx, int8_t& dy) {
  dx = 0;
  dy = 0;
  switch (d) {
    case shared::MazeDirection::UP:
      dy = 1;
      break;
    case shared::MazeDirection::DOWN:
      dy = -1;
      break;
    case shared::MazeDirection::LEFT:
      dx = -1;
      break;
    case shared::MazeDirection::RIGHT:
      dx = 1;
      break;
    default:
      break;
  }
}

// Must match client `computeCamera` maze FPV: slot -> world XY "look" /
// forward.
shared::MazeDirection mazeDirectionForJoinSlot(uint8_t slot) {
  switch (slot) {
    case 1:
      return shared::MazeDirection::UP;
    case 2:
      return shared::MazeDirection::DOWN;
    case 3:
      return shared::MazeDirection::LEFT;
    case 4:
      return shared::MazeDirection::RIGHT;
    default:
      return shared::MazeDirection::UP;
  }
}

void mazeFacingFromJoinSlot(uint8_t slot, float& sx, float& sy) {
  int8_t dx = 0;
  int8_t dy = 0;
  mazeDirToDelta(mazeDirectionForJoinSlot(slot), dx, dy);
  sx = static_cast<float>(dx);
  sy = static_cast<float>(dy);
}

}  // namespace

void EnterMazePuzzle(ServerGame& game) {
  entt::entity winter = findWinterSection(game.registry);
  if (winter == entt::null) return;

  const auto& section = game.registry.get<shared::SectionController>(winter);
  entt::entity puzzleEnt =
      findEntityByNumericId(game.registry, section.puzzleID);
  if (puzzleEnt == entt::null ||
      !game.registry.all_of<shared::PuzzleComponent>(puzzleEnt))
    return;

  auto& puzzle = game.registry.get<shared::PuzzleComponent>(puzzleEnt);
  if (puzzle.phase == shared::RunPhase::LOBBY) {
    puzzle.phase = shared::RunPhase::INPROGRESS;
  }

  for (auto e : game.registry.view<shared::RunState>()) {
    auto& run = game.registry.get<shared::RunState>(e);
    if (run.phase == shared::RunPhase::LOBBY)
      run.phase = shared::RunPhase::INPROGRESS;
  }

  game.registry.emplace_or_replace<shared::MazeUIState>(puzzleEnt, true);
  printf("[GameLogic] EnterMazePuzzle: puzzle id %" PRIu32
         " -> INPROGRESS, maze UI on\n",
         section.puzzleID);
}

void ExitMazePuzzle(ServerGame& game) {
  entt::entity winter = findWinterSection(game.registry);
  if (winter == entt::null) return;

  const auto& section = game.registry.get<shared::SectionController>(winter);
  entt::entity puzzleEnt =
      findEntityByNumericId(game.registry, section.puzzleID);
  if (puzzleEnt == entt::null) return;
  if (!game.registry.all_of<shared::PuzzleComponent>(puzzleEnt)) return;

  const auto& puzzle = game.registry.get<shared::PuzzleComponent>(puzzleEnt);
  if (puzzle.phase != shared::RunPhase::FINISHED) return;

  if (game.registry.all_of<shared::MazeUIState>(puzzleEnt)) {
    game.registry.get<shared::MazeUIState>(puzzleEnt).open = false;
  }
  printf("[GameLogic] ExitMazePuzzle: maze UI cleared (puzzle was FINISHED)\n");
}

bool StepMemorySpirit(ServerGame& game, uint32_t puzzleEntityId,
                      entt::entity spiritEnt, int8_t ddx, int8_t ddy,
                      int8_t gridW, int8_t gridH, uint64_t wallMask,
                      uint32_t badMovePenaltyMs) {
  if (gridW <= 0 || gridH <= 0) return false;
  entt::entity puzzleEnt = findEntityByNumericId(game.registry, puzzleEntityId);
  if (puzzleEnt == entt::null ||
      !game.registry.all_of<shared::PuzzleComponent>(puzzleEnt))
    return false;

  auto& puzzle = game.registry.get<shared::PuzzleComponent>(puzzleEnt);
  if (puzzle.phase != shared::RunPhase::INPROGRESS) return false;

  if (!game.registry.all_of<shared::MazeSpiritGrid>(spiritEnt)) return false;
  auto& grid = game.registry.get<shared::MazeSpiritGrid>(spiritEnt);

  const auto nx = static_cast<int8_t>(grid.gx + ddx);
  const auto ny = static_cast<int8_t>(grid.gy + ddy);

  auto applyPenalty = [&]() {
    if (badMovePenaltyMs == 0) return;
    uint64_t sum =
        static_cast<uint64_t>(puzzle.puzzleElapsedTimeMs) + badMovePenaltyMs;
    puzzle.puzzleElapsedTimeMs =
        sum > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(sum);
  };

  if (nx < 0 || ny < 0 || nx >= gridW || ny >= gridH) {
    applyPenalty();
    return false;
  }

  const int idx =
      static_cast<int>(ny) * static_cast<int>(gridW) + static_cast<int>(nx);
  if (idx >= 0 && idx < 64 && ((wallMask >> idx) & 1ull) != 0) {
    applyPenalty();
    return false;
  }

  grid.gx = nx;
  grid.gy = ny;
  return true;
}

void ClaimDirectionPad(ServerGame& game, uint32_t puzzleEntityId,
                       entt::entity p0, entt::entity p1, entt::entity p2,
                       entt::entity p3,
                       const std::array<shared::MazeDirection, 4>& padOrder) {
  entt::entity puzzleEnt = findEntityByNumericId(game.registry, puzzleEntityId);
  if (puzzleEnt == entt::null ||
      !game.registry.all_of<shared::PuzzleComponent>(puzzleEnt))
    return;

  const auto& puzzle = game.registry.get<shared::PuzzleComponent>(puzzleEnt);
  if (puzzle.phase != shared::RunPhase::INPROGRESS) return;

  const entt::entity players[4] = {p0, p1, p2, p3};

  for (int i = 0; i < 4; i++) {
    if (players[i] == entt::null) continue;
    game.registry.emplace_or_replace<shared::MazePadBinding>(
        players[i], padOrder[static_cast<size_t>(i)]);
  }
}

void ClaimDirectionPad(ServerGame& game, uint32_t puzzleEntityId,
                       entt::entity p0, entt::entity p1, entt::entity p2,
                       entt::entity p3) {
  static constexpr std::array<shared::MazeDirection, 4> kDefault{
      shared::MazeDirection::UP, shared::MazeDirection::DOWN,
      shared::MazeDirection::LEFT, shared::MazeDirection::RIGHT};
  ClaimDirectionPad(game, puzzleEntityId, p0, p1, p2, p3, kDefault);
}

void CollectMazeFragment(ServerGame& game) {
  entt::entity winter = findWinterSection(game.registry);
  if (winter == entt::null) return;

  auto& winterSection = game.registry.get<shared::SectionController>(winter);
  entt::entity puzzleEnt =
      findEntityByNumericId(game.registry, winterSection.puzzleID);
  if (puzzleEnt == entt::null ||
      !game.registry.all_of<shared::PuzzleComponent>(puzzleEnt))
    return;

  auto& puzzle = game.registry.get<shared::PuzzleComponent>(puzzleEnt);
  if (puzzle.phase == shared::RunPhase::FINISHED) return;

  puzzle.phase = shared::RunPhase::FINISHED;
  winterSection.completed = true;

  for (auto e : game.registry.view<shared::GameSection>()) {
    auto& gs = game.registry.get<shared::GameSection>(e);
    if (gs.sectionsCompleted < 255) gs.sectionsCompleted++;
  }
  printf(
      "[GameLogic] CollectMazeFragment: winter done, sections completed++\n");

  // Reveal the winter fragment now that the puzzle is solved.
  auto fragView =
      game.registry.view<shared::FragmentComponent, shared::OverworldTag>();
  for (auto fragEnt : fragView) {
    if (game.registry.get<shared::FragmentComponent>(fragEnt).season !=
        shared::SectionSeasonMap::WINTER)
      continue;
    if (!game.registry.all_of<shared::RenderInfo>(fragEnt)) {
      game.registry.emplace<shared::RenderInfo>(fragEnt, "light_cube", 1.0f,
                                                1.0f, 1.0f);
    }
    break;
  }
}

bool HasUnlockedWinterSection(const ServerGame& game) {
  auto view = game.registry.view<shared::SectionController>();
  for (auto e : view) {
    const auto& sc = view.get<shared::SectionController>(e);
    if (sc.type == shared::SectionSeasonMap::WINTER && sc.unlocked) return true;
  }
  return false;
}

uint32_t GetWinterPuzzleNumericId(ServerGame& game) {
  entt::entity winter = findWinterSection(game.registry);
  if (winter == entt::null) return 0;
  return game.registry.get<shared::SectionController>(winter).puzzleID;
}

void ClaimPadsForActivePlayers(ServerGame& game, uint32_t puzzleEntityId) {
  entt::entity puzzleEnt = findEntityByNumericId(game.registry, puzzleEntityId);
  if (puzzleEnt == entt::null ||
      !game.registry.all_of<shared::PuzzleComponent>(puzzleEnt))
    return;
  if (game.registry.get<shared::PuzzleComponent>(puzzleEnt).phase !=
      shared::RunPhase::INPROGRESS)
    return;

  printf("[GameLogic] ClaimPadsForActivePlayers puzzle %" PRIu32
         " (facing from join slot, matches client camera)\n",
         puzzleEntityId);
  for (auto& kv : game.active_players) {
    entt::entity m = kv.second.maze_avatar;
    if (m == entt::null || !game.registry.valid(m)) continue;
    if (!game.registry.all_of<shared::RenderInfo>(m)) continue;
    uint8_t slot = game.registry.get<shared::RenderInfo>(m).playerSlot;
    if (slot < 1 || slot > 4) slot = 1;
    const auto pad = mazeDirectionForJoinSlot(slot);
    game.registry.emplace_or_replace<shared::MazePadBinding>(m, pad);
    uint32_t eid = game.registry.get<shared::Entity>(m).id;
    printf("[GameLogic]   entity %" PRIu32 " slot=%" PRIu8
           " pad=%d (UP key = this facing)\n",
           eid, slot, static_cast<int>(pad));
  }
}

void TickMazeExploration(ServerGame& game, float dt) {
  (void)dt;

  const entt::entity spirit = maze_spirit_control::findSharedSpirit(game);
  if (spirit == entt::null) return;

  auto inputView = game.registry.view<shared::PlayerInput, shared::MazeTag>();
  for (auto ent : inputView) {
    const auto& in = game.registry.get<shared::PlayerInput>(ent);
    if (in.keys_newly_pressed & KEY_MAZE_COLLECT) {
      CollectMazeFragment(game);
      return;
    }
  }

  if (!game.registry.all_of<shared::PhysicsBody>(spirit)) return;
  auto& bodyInterface = game.physics.getBodyInterface();
  auto& spiritPb = game.registry.get<shared::PhysicsBody>(spirit);
  JPH::BodyID spiritBody(spiritPb.bodyId);
  if (!bodyInterface.IsAdded(spiritBody)) return;

  if (!mazePuzzleAllowsSpiritMove(game)) {
    bodyInterface.SetLinearVelocity(spiritBody, JPH::Vec3::sZero());
    if (game.registry.all_of<shared::Velocity>(spirit)) {
      auto& vel = game.registry.get<shared::Velocity>(spirit);
      vel.dx = vel.dy = vel.dz = 0.f;
    }
    return;
  }

  const maze_spirit_control::SpiritDrive drive =
      maze_spirit_control::collectSpiritDriveFromPlayers(game);
  maze_spirit_control::applySpiritDriveVelocity(game, spirit, drive);
}

void ResetMazeSpiritSpawn(ServerGame& game) {
  for (auto e :
       game.registry.view<shared::MazeSpiritGrid, shared::PhysicsBody>()) {
    auto& bodyInterface = game.physics.getBodyInterface();
    auto& pb = game.registry.get<shared::PhysicsBody>(e);
    JPH::BodyID bodyId(pb.bodyId);
    if (!bodyInterface.IsAdded(bodyId)) continue;

    bodyInterface.SetPosition(bodyId, JPH::RVec3(0.0, 0.0, 0.0),
                              JPH::EActivation::Activate);
    bodyInterface.SetLinearVelocity(bodyId, JPH::Vec3::sZero());
    bodyInterface.SetAngularVelocity(bodyId, JPH::Vec3::sZero());

    auto& grid = game.registry.get<shared::MazeSpiritGrid>(e);
    grid.gx = 0;
    grid.gy = 0;
    if (game.registry.all_of<shared::Position>(e)) {
      auto& pos = game.registry.get<shared::Position>(e);
      pos.x = 0.f;
      pos.y = 0.f;
      pos.z = 0.f;
    }
    if (game.registry.all_of<shared::Velocity>(e)) {
      auto& v = game.registry.get<shared::Velocity>(e);
      v.dx = v.dy = v.dz = 0.f;
    }
  }
}

void tickMazeGameLogic(ServerGame& /*game*/, float /*dt*/) {}
