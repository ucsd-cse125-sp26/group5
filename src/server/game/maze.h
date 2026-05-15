#pragma once

#include <array>
#include <cstdint>
#include <entt/entity/fwd.hpp>

#include "shared/components.h"

struct ServerGame;

void EnterMazePuzzle(ServerGame& game);
void ExitMazePuzzle(ServerGame& game);

// Grid uses cells [0, gridW) x [0, gridH). wallMask bit (gy * gridW + gx) == 1
// => blocked. Returns false if move rejected; optional badMovePenaltyMs added
// to puzzle elapsed time.
bool StepMemorySpirit(ServerGame& game, uint32_t puzzleEntityId,
                      entt::entity spiritEnt, int8_t ddx, int8_t ddy,
                      int8_t gridW, int8_t gridH, uint64_t wallMask,
                      uint32_t badMovePenaltyMs = 0);

// Assigns one direction per player slot from padOrder (e.g. shuffled). No-op if
// puzzle not INPROGRESS.
void ClaimDirectionPad(ServerGame& game, uint32_t puzzleEntityId,
                       entt::entity p0, entt::entity p1, entt::entity p2,
                       entt::entity p3,
                       const std::array<shared::MazeDirection, 4>& padOrder);
// Fixed order: Up, Down, Left, Right for p0..p3 (tests / explicit layout).
void ClaimDirectionPad(ServerGame& game, uint32_t puzzleEntityId,
                       entt::entity p0, entt::entity p1, entt::entity p2,
                       entt::entity p3);

// Winter maze solved: puzzle FINISHED, winter section completed, section
// progress bumped.
void CollectMazeFragment(ServerGame& game);

// True if a WINTER SectionController exists and is unlocked (puzzle id may
// legitimately be 0).
bool HasUnlockedWinterSection(const ServerGame& game);

// Winter maze puzzle entity id (shared::Entity id), or 0 if no unlocked winter
// section.
uint32_t GetWinterPuzzleNumericId(ServerGame& game);

// Sets MazePadBinding from each client's join slot so UP = their maze FPV
// facing (same as computeCamera).
void ClaimPadsForActivePlayers(ServerGame& game, uint32_t puzzleEntityId);

// Per-tick: shared spirit uses Jolt velocity from UP; direction = that client's
// join-slot facing (FPV). G = collect fragment.
void TickMazeExploration(ServerGame& game, float dt = 1.0f / 60.0f);

// Teleport spirit to maze origin; call when entering maze state.
void ResetMazeSpiritSpawn(ServerGame& game);

void tickMazeGameLogic(ServerGame& game, float dt);
