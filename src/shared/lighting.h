#pragma once

namespace shared {

// 1 / (constant + linear·d + quadratic·d²).
struct PointLightAttenuation {
  float constant;
  float linear;
  float quadratic;
};

// learnopengl ~50-unit falloff. constant=1 avoids the divide-by-zero at d=0.
inline constexpr PointLightAttenuation kDefaultPointLightAttenuation{
    .constant = 1.0f, .linear = 0.09f, .quadratic = 0.032f};

// Falls to near-zero at `range`; constants from the learnopengl table.
// glTF stores this as KHR_lights_punctual `range`; assimp surfaces it on
// the node's metadata as "PBR_LightRange".
inline PointLightAttenuation attenuationForRange(float range) {
  if (range <= 0.0f) return kDefaultPointLightAttenuation;
  return {.constant = 1.0f,
          .linear = 4.5f / range,
          .quadratic = 75.0f / (range * range)};
}

}  // namespace shared
