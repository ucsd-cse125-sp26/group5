#pragma once

namespace shared {

// Phong attenuation triple: 1 / (constant + linear·d + quadratic·d²).
struct PointLightAttenuation {
  float constant;
  float linear;
  float quadratic;
};

// Default for point lights with no specified range. Tuned for a useful range
// of ~50 world units before the contribution becomes negligible — matches the
// learnopengl.com attenuation table and produces sensible falloff at the
// scales the map's test assets use. Constant=1 avoids the divide-by-zero
// blowup at the source that pure inverse-square attenuation has.
inline constexpr PointLightAttenuation kDefaultPointLightAttenuation{
    .constant = 1.0f, .linear = 0.09f, .quadratic = 0.032f};

// Approximates Phong attenuation that falls to near-zero at `range` units
// from the source, derived from the standard relation
//   1 / (1 + 4.5/r · d + 75/r² · d²)
// where the linear/quadratic constants are interpolated from the learnopengl
// table by `range`. glTF's KHR_lights_punctual exposes this as the optional
// `range` cutoff; assimp drops it from aiLight and stores it on the owning
// node's metadata as "PBR_LightRange".
inline PointLightAttenuation attenuationForRange(float range) {
  if (range <= 0.0f) return kDefaultPointLightAttenuation;
  return {.constant = 1.0f,
          .linear = 4.5f / range,
          .quadratic = 75.0f / (range * range)};
}

}  // namespace shared
