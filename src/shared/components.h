#pragma once
#include <cstdint>
#include <string>
namespace shared {
struct Position {
  float x, y, z;
  float qw, qx, qy, qz;
};

struct Velocity {
  float dx, dy, dz;
};

struct RenderInfo {
  std::string modelName;
  float scale;
};

struct Camera {
  float pitch;
  float ht;
};

struct Entity {
  uint32_t id;
};

struct PlayerInput {
  uint8_t keys;
};

}  // namespace shared
