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

void main() {
  vec4 diffuse = texture(material.diffuse, vTexCoords);
  if (diffuse.a < 0.5) discard;
  gPosition = vec4(worldPos, 1.0);
  gNormal = vec4(normalize(worldNormal), material.shininess);
  gAlbedo = vec4(diffuse.rgb, 1.0);
  gSpecular = vec4(texture(material.specular, vTexCoords).rgb, 1.0);
  gEmissive = vec4(texture(material.emissive, vTexCoords).rgb, 1.0);
}
