#version 410 core
layout(location = 0) in vec3 position;

uniform mat4 model;
uniform mat4 shadowMatrix;

out vec4 fragWorldPos;

void main() {
  fragWorldPos = model * vec4(position, 1.0);
  gl_Position = shadowMatrix * fragWorldPos;
}
