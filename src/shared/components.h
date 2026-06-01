#pragma once
#include <cstdint>
#include <glm/vec3.hpp>
#include <string>

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

// Picks which animation clip a model should be playing. Empty clipName
// falls back to the first clip in the model's AnimationLibrary. The client
// drives playhead time off its own frame dt; startTickMs is reserved for a
// later authoritative-sync pass but currently unused.
struct AnimationState {
  std::string clipName;
  uint32_t startTickMs = 0;
  bool loop = true;
};

struct RenderInfo {
  std::string modelName;
  float sx = 1.0f;
  float sy = 1.0f;
  float sz = 1.0f;
  // 1-4 = join order for player avatars (shown on cube top); 0 = no slot label.
  uint8_t playerSlot = 0;
};

struct ColorBoundingBox {
  float minX = 0.0f;
  float minY = 0.0f;
  float minZ = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
  float maxZ = 0.0f;
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
  uint32_t rotateTargetId = 0;
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

// Sound stuff
enum class SoundPlayMode : uint8_t {
  POSITIONAL,
  AMBIENT,
};

enum class SoundTriggerType : uint8_t {
  ALWAYS,     // loop forever while entity exists
  PROXIMITY,  // loop while player within range
  ON_EVENT,   // one-shot via SoundEventPacket
};

struct SoundLayer {
  uint32_t soundId = 0;
  SoundTriggerType trigger = SoundTriggerType::ALWAYS;
  SoundPlayMode playMode = SoundPlayMode::POSITIONAL;
  float volume = 1.0f;
  float proximityRange = 10.0f;
  float fadeSpeed = 2.0f;

  constexpr static auto serialize(auto& archive, auto& self) {
    return archive(self.soundId, self.trigger, self.playMode, self.volume,
                   self.proximityRange, self.fadeSpeed);
  }
};

// shared/components.h
struct SoundEmitter {
  static constexpr uint8_t MAX_LAYERS = 4;
  SoundLayer layers[MAX_LAYERS];
  uint8_t layerCount = 0;

  constexpr static auto serialize(auto& archive, auto& self) {
    return archive(self.layers, self.layerCount);
  }
};

// Sound detection for landing
struct Grounded {
  bool isGrounded = false;
  bool wasGrounded = false;  // last frame's value, for edge detection
};

enum class RunPhase : uint8_t { LOBBY, INPROGRESS, FINISHED };

enum class Outcome : uint8_t { UNDECIDED, WIN, LOSE };

enum class SectionSeasonMap : uint8_t { WINTER, FALL, SUMMER, SPRING };

struct RunState {
  RunPhase phase = RunPhase::LOBBY;
  Outcome outcome = Outcome::UNDECIDED;
};

struct GameSection {
  SectionSeasonMap currentActiveSeason = SectionSeasonMap::WINTER;
  uint8_t sectionsCompleted = 0;  // count 0 to 4
};

enum class OverworldPuzzleKind : uint8_t {
  None = 0,
  Maze = 1,
  Tangram = 2,
};

struct PuzzleComponent {
  RunPhase phase = RunPhase::LOBBY;
  uint32_t puzzleElapsedTimeMs = 0;
  uint32_t puzzleTimeLimitMs = 0;
  OverworldPuzzleKind overworldKind = OverworldPuzzleKind::None;
};

enum class MazeDirection : uint8_t { NONE = 0, UP, DOWN, LEFT, RIGHT };

struct MazeSpiritGrid {
  int8_t gx = 0;
  int8_t gy = 0;
};

struct MazePadBinding {
  MazeDirection pad = MazeDirection::NONE;
};

struct MazeUIState {
  bool open = false;
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
};

struct SwitchComponent {
  uint32_t parent =
      0;  // what entity this switch is linked to (door, puzzle, etc)
  bool switchOn = false;
};

// Tag component — marks this entity as a section barrier
struct SectionBarrierTag {
  uint8_t sectionID;  // matches SectionController's puzzleID
  glm::vec3 halfExtents;
  SectionSeasonMap season;
};

// Flag component — added to a barrier when it should be torn down
// OverworldState::update sees this and calls destroyBody
struct SectionBarrierPendingRemoval {};

struct SectionBarrierVisible {
};  // tag — present = has RenderInfo, absent = invisible

struct FragmentComponent {
  SectionSeasonMap season;
  bool isPickedUp;
};

// Overworld mini-board puzzle (four players, shared green piece).
struct OverworldMazePuzzleState {
  bool active = false;
  bool completed = false;
};

struct OverworldMazePiece {};

struct TangramPiece {
  uint8_t pieceId = 0;       // 1–7
  bool slotSnapped = false;  // XY locked to slot; yaw still player-controlled
};

struct OverworldTangramPiece {};

// Ghost outline on the goal square (target pose per piece id 1–7).
struct TangramSlotGhost {
  uint8_t pieceId = 0;
};

// Tangram push puzzle active flag (synced to clients).
struct OverworldTangramPuzzleState {
  bool active = false;
  // shared::tangram_roles::kIsolationStage while active; 0 = full access.
  uint8_t roleIsolationStage = 0;
};

struct TangramLayoutVisual {};

struct MazeLayoutVisual {};

// Server-only: marks a falling hazard cube. `age` drives lifetime cleanup so
// objects that land and rest on the floor don't accumulate forever.
struct FallingObject {
  float age = 0.0f;
};

// Server-only: a marker entity that rains FallingObjects within `radius` of its
// own Position. `timer` is a per-zone spawn accumulator.
struct FallingHazardZone {
  float radius = 8.0f;
  float spawnHeight = 20.0f;  // spawn this far above the zone's Position.z
  float interval = 0.4f;      // seconds between drops
  float timer = 0.0f;
};

struct Knockback {
  float remaining =
      0.0f;              // seconds left where physics owns horizontal velocity
  bool contact = false;  // overlapping a cube last frame (for edge detection)
  bool justHit =
      false;  // true only on the frame contact begins; meter reads it
  float lastDirX = 1.0f;  // last horizontal push dir, reused for overhead hits
  float lastDirY = 0.0f;
};

// Fall section "don't get hit" challenge. One controller entity holds all four
// quarters; replicated so each client can draw its own slot. Index = slot - 1.
struct FallChallengeState {
  bool active = false;
  bool completed = false;       // set true on solve; blocks re-triggering
  uint8_t participantMask = 0;  // bit i set => player slot (i+1) is playing
  float fill[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // 0..1 per player slot
  float fillRate = 0.05f;    // regained per second while NOT being hit
  float hitPenalty = 0.25f;  // lost per hit

  constexpr static auto serialize(auto& archive, auto& self) {
    return archive(self.active, self.completed, self.participantMask, self.fill,
                   self.fillRate, self.hitPenalty);
  }
};

// Server-only: per-player ring of recent hit timestamps, used to detect
// "4 hits within 4 seconds". Not registered for replication.
struct FallHitWindow {
  float times[4] = {-1000.0f, -1000.0f, -1000.0f, -1000.0f};
  int head = 0;
};
}  // namespace shared
