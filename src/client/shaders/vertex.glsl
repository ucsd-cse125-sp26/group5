#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoords;

out vec3 normalFromVert;
out vec2 texCoordsFromVert;
out vec3 fragPos;

uniform mat4 projection;
uniform mat4 model;
uniform mat4 view;
uniform mat3 normalMatrix;

void main() {
  gl_Position = projection * view * model * vec4(position, 1.0);
  fragPos = vec3(model * vec4(position, 1.0));
  normalFromVert = normalMatrix * normal;
  texCoordsFromVert = texCoords;
}
