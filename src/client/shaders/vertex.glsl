#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoords;

out vec3 normalFromVert;
out vec2 texCoordsFromVert;
out vec3 fragPos;
out vec4 fragPosLightSpace;

uniform mat4 projection;
uniform mat4 model;
uniform mat4 view;
uniform mat3 normalMatrix;
uniform mat4 lightSpaceMatrix;

void main() {
  vec4 world = model * vec4(position, 1.0);
  gl_Position = projection * view * world;
  fragPos = world.xyz;
  normalFromVert = normalMatrix * normal;
  texCoordsFromVert = texCoords;
  fragPosLightSpace = lightSpaceMatrix * world;
}
