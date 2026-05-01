#version 410 core
layout(location = 0) in vec3 position;

uniform mat4 model;

void main() {
  // World-space output. The geometry shader does the per-face light-space
  // transform.
  gl_Position = model * vec4(position, 1.0);
}
