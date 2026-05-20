#version 410 core
in vec3 worldPos;
in vec3 worldNormal;
in vec2 vTexCoords;

// gPosition: .rgb world pos, .a=1 sentinel ("real geometry, not sky").
// gNormal:   .rgb world normal, .a shininess.
// gAlbedo/gSpecular/gEmissive: .rgb sampled material; .a unused.
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec4 gSpecular;
layout(location = 4) out vec4 gEmissive;

struct Material {
  sampler2D ambient;
  sampler2D diffuse;
  sampler2D specular;
  sampler2D emissive;
  float shininess;
};
uniform Material material;

// 0 = off; >=2 posterizes each RGB channel to N evenly-spaced levels.
uniform int textureQuantizeLevels;

vec3 quantizeRGB(vec3 c) {
  if (textureQuantizeLevels < 2) return c;
  // Quantize the HSV value (max channel) and rescale all channels by the
  // same factor. Per-channel posterization snaps each channel to its own
  // plateau, which shifts hues toward whichever primary dominates and
  // pumps saturation; this scheme keeps hue and saturation untouched and
  // only discretizes brightness.
  float L = float(textureQuantizeLevels);
  float v = max(c.r, max(c.g, c.b));
  if (v < 1e-5) return c;
  // round() centers each plateau in its snap range; floor would bias the
  // whole image upward in brightness.
  float qv = clamp(round(v * (L - 1.0)) / max(L - 1.0, 1.0), 0.0, 1.0);
  return c * (qv / v);
}

void main() {
  vec4 diffuse = texture(material.diffuse, vTexCoords);
  if (diffuse.a < 0.5) discard;
  gPosition = vec4(worldPos, 1.0);
  gNormal = vec4(normalize(worldNormal), material.shininess);
  gAlbedo = vec4(quantizeRGB(diffuse.rgb), 1.0);
  gSpecular = vec4(quantizeRGB(texture(material.specular, vTexCoords).rgb), 1.0);
  gEmissive = vec4(quantizeRGB(texture(material.emissive, vTexCoords).rgb), 1.0);
}
