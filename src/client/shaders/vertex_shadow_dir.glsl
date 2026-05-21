#version 410 core
layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoords;

out vec2 vTexCoords;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;

void main() {
  vTexCoords = texCoords;
  gl_Position = lightSpaceMatrix * model * vec4(position, 1.0);
}
