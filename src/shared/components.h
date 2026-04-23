#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include <cstdint>
#include <string>

#include "input.h"

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
};

struct DirectionalLight {
  float dirX, dirY, dirZ;
  float ambientR, ambientG, ambientB;
  float diffuseR, diffuseG, diffuseB;
  float specularR, specularG, specularB;
};

// struct PhysicsBody {
//   JPH::BodyID bodyId;
// };
struct PhysicsBody {
  uint32_t bodyId;
};

}  // namespace shared
