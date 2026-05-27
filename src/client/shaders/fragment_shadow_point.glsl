#version 410 core
in vec3 fragWorldPos;
in vec2 vTexCoords;

struct Material {
  sampler2D diffuse;
};
uniform Material material;
uniform vec3 lightPos;
uniform float pointFarPlane;
uniform float alphaCutoff;

void main() {
  if (texture(material.diffuse, vTexCoords).a < alphaCutoff) discard;
  float d = length(fragWorldPos - lightPos);
  gl_FragDepth = clamp(d / pointFarPlane, 0.0, 1.0);
}
