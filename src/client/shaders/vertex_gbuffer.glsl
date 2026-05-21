#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoords;
layout(location = 4) in vec3 tangent;

out vec3 worldPos;
out vec3 worldNormal;
out vec3 worldTangent;
out vec2 vTexCoords;

layout(std140) uniform CameraBlock {
  mat4 view;
  mat4 projection;
  mat4 lightSpaceMatrix;
  vec3 viewPos;
  float pointFarPlane;
} camera;

uniform mat4 model;
uniform mat3 normalMatrix;

void main() {
  vec4 world = model * vec4(position, 1.0);
  gl_Position = camera.projection * camera.view * world;
  worldPos = world.xyz;
  worldNormal = normalMatrix * normal;
  worldTangent = normalMatrix * tangent;
  vTexCoords = texCoords;
}
