#version 410 core
layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoords;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;

out vec3 fragWorldPos;
out vec2 vTexCoords;

void main() {
  vec4 world = model * vec4(position, 1.0);
  fragWorldPos = world.xyz;
  vTexCoords = texCoords;
  gl_Position = lightSpaceMatrix * world;
}
