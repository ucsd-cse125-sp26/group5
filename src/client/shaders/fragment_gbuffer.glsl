#version 410 core
in vec3 worldPos;
in vec3 worldNormal;
in vec2 vTexCoords;

// MRT outputs:
//   gPosition.rgb = world-space position; .a = 1 to flag "real geometry"
//                   so the lighting pass can discard sky pixels.
//   gNormal.rgb   = world-space normal (renormalized).
//   gNormal.a     = material shininess.
//   gAlbedo.rgb   = sampled diffuse / albedo. .a unused.
//   gSpecular.rgb = sampled specular tint (full RGB so colored highlights
//                   from e.g. metals work). .a unused.
//   gEmissive.rgb = sampled emissive (added unattenuated in lighting).
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

void main() {
  gPosition = vec4(worldPos, 1.0);
  gNormal = vec4(normalize(worldNormal), material.shininess);
  gAlbedo = vec4(texture(material.diffuse, vTexCoords).rgb, 1.0);
  gSpecular = vec4(texture(material.specular, vTexCoords).rgb, 1.0);
  gEmissive = vec4(texture(material.emissive, vTexCoords).rgb, 1.0);
}
