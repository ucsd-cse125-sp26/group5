#version 410 core
layout(location = 0) in vec3 position;

uniform mat4 model;

void main() {
  // GS does the per-face light-space transform.
  gl_Position = model * vec4(position, 1.0);
}
