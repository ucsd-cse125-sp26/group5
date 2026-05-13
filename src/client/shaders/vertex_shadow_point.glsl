#version 410 core
layout(location = 0) in vec3 position;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;

out vec3 fragWorldPos;

void main() {
  vec4 world = model * vec4(position, 1.0);
  fragWorldPos = world.xyz;
  gl_Position = lightSpaceMatrix * world;
}
