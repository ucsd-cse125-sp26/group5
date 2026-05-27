#version 410 core
in vec2 vTexCoords;

// Match the gbuffer alpha cutout so cutout meshes (foliage, fences, etc.)
// don't cast solid-box shadows. material.diffuse is bound by Draw() to the
// same texture unit used in the main pass.
struct Material {
  sampler2D diffuse;
};
uniform Material material;
uniform float alphaCutoff;

void main() {
  if (texture(material.diffuse, vTexCoords).a < alphaCutoff) discard;
}
