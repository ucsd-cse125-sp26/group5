#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "input.h"

namespace shared {

// tags
struct OverworldTag {};
struct MazeTag {};

struct Position {
  float x, y, z;
  float qw, qx, qy, qz;
};

struct Velocity {
  float dx, dy, dz;
};

struct RenderInfo {
  std::string modelName;
  float sx, sy, sz;
};

struct Camera {
  float pitch;
  float ht;
};

struct Entity {
  uint32_t id;
};

struct PlayerInput {
  InputKeys keys;
  InputKeys keys_prev;
  InputKeys keys_newly_pressed;
  float mouseDx;
  float mouseDy;
};

struct PointLight {
  float px, py, pz;
  float constant;
  float linear;
  float quadratic;
  float ambientR, ambientG, ambientB;
  float diffuseR, diffuseG, diffuseB;
  float specularR, specularG, specularB;
  bool castsShadow = true;
};

struct DirectionalLight {
  float dirX, dirY, dirZ;
  float ambientR, ambientG, ambientB;
  float diffuseR, diffuseG, diffuseB;
  float specularR, specularG, specularB;
};

struct Scene {
  std::string name;
};

// struct PhysicsBody {
//   JPH::BodyID bodyId;
// };
struct PhysicsBody {
  uint32_t bodyId;
};

enum class RunPhase : uint8_t { LOBBY, INPROGRESS, FINISHED };

enum class Outcome : uint8_t { UNDECIDED, WIN, LOSE };

enum class MiniGameType : uint8_t {
  NONE,
  BALL_DEMO,
  MAZE,
};

enum class MiniGamePhase : uint8_t {
  INACTIVE,
  WAITING_FOR_PLAYER,
  RUNNING,
  COMPLETE,
  FAILED,
};

struct MiniGameSession {
  uint32_t sessionId = 0;
  uint32_t surfaceEntityId = 0;
  MiniGameType type = MiniGameType::NONE;
  MiniGamePhase phase = MiniGamePhase::INACTIVE;
  uint32_t elapsedMs = 0;
  uint32_t timeLimitMs = 0;
  float logicalWidth = 1.0f;
  float logicalHeight = 1.0f;
};

struct MiniGame2D {
  uint32_t sessionId = 0;
  uint8_t layer = 0;
};

enum class Renderable2DType : uint8_t {
  RECT = 0,
  SPRITE = 1,
  TEXT = 2,
  TILEMAP = 3,
};

struct Renderable2D {
  Renderable2DType type = Renderable2DType::RECT;
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
  std::string spriteName;
};

struct TextRenderable2D {
  char text[64] = {};
  float fontSize = 16.0f;
};

struct TilemapRenderable2D {
  uint8_t cols = 0;
  uint8_t rows = 0;
  std::vector<uint8_t> tiles;
};

enum class SectionSeasonMap : uint8_t { WINTER, FALL, SUMMER, SPRING };

struct RunState {
  RunPhase phase = RunPhase::LOBBY;
  Outcome outcome = Outcome::UNDECIDED;
};

struct GameSection {
  SectionSeasonMap currentActiveSeason = SectionSeasonMap::WINTER;
  uint8_t sectionsCompleted = 0;  // count 0 to 4
};

struct PuzzleComponent {
  RunPhase phase = RunPhase::LOBBY;
  uint32_t puzzleElapsedTimeMs = 0;
  uint32_t puzzleTimeLimitMs = 0;
};

struct TimeComponent {
  uint32_t elapsedTimeMs = 0;
  uint32_t timeLimitMs = 0;
};

struct SectionController {
  SectionSeasonMap type =
      SectionSeasonMap::WINTER;  // the season map of this section
  uint32_t puzzleID = 0;         // the puzzle linked to this section
  bool unlocked = false;
  bool completed = false;
};

enum class DoorState : uint8_t {
  CLOSED = 0,
  OPENING = 1,  // Currently moving, collision is still active
  OPEN = 2,     // Fully open, players can pass
  CLOSING = 3   // Moving back down
};

struct SectionDoorComponent {
  DoorState state = DoorState::CLOSED;
  uint8_t requiredPlayers = 4;
  uint32_t sectionID = 0;  // the section this door is linked to

  // track the physical animation
  float currentZ = 0.0f;
  float targetOpenZ = -10.0f;  // where the door should end up
};

struct SwitchComponent {
  uint32_t parent =
      0;  // what entity this switch is linked to (door, puzzle, etc)
  bool switchOn = false;
};

}  // namespace shared
