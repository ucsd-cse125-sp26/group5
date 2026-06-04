#version 410 core

// In-world video screen: a unit quad transformed by `model`, sharing the
// engine's CameraBlock UBO for view/projection.
layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoords;

out vec2 vUV;

layout(std140) uniform CameraBlock {
  mat4 view;
  mat4 projection;
  mat4 lightSpaceMatrices[K_SHADOW_CASCADE_COUNT];
  vec4 cascadeSplits;
  vec3 viewPos;
  float pointFarPlane;
} camera;

uniform mat4 model;

void main() {
  gl_Position = camera.projection * camera.view * model * vec4(position, 1.0);
  vUV = texCoords;
}
