#version 410 core
layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoords;
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4 weights;

out vec2 vTexCoords;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;
uniform mat4 finalBonesMatrices[K_MAX_BONES];
uniform int useSkinning;

void main() {
  vec4 localPos = vec4(position, 1.0);
  if (useSkinning != 0) {
    vec4 skinned = vec4(0.0);
    for (int i = 0; i < 4; ++i) {
      int b = boneIDs[i];
      if (b < 0 || b >= K_MAX_BONES) continue;
      skinned += (finalBonesMatrices[b] * vec4(position, 1.0)) * weights[i];
    }
    // Bind-pose fallback for unweighted vertices (otherwise origin collapse).
    if (skinned.w > 0.0) localPos = skinned;
  }
  vTexCoords = texCoords;
  gl_Position = lightSpaceMatrix * model * localPos;
}
