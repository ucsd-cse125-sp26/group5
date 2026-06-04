#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoords;
layout(location = 4) in vec3 tangent;
// Skinning attributes — only read when useSkinning != 0. Locations 6/7 are
// past tangent/bitangent so non-skinned shaders are unaffected.
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4 weights;

out vec3 worldPos;
out vec3 worldNormal;
out vec3 worldTangent;
out vec2 vTexCoords;

layout(std140) uniform CameraBlock {
  mat4 view;
  mat4 projection;
  mat4 lightSpaceMatrices[K_SHADOW_CASCADE_COUNT];
  vec4 cascadeSplits;
  vec3 viewPos;
  float pointFarPlane;
} camera;

uniform mat4 model;
uniform mat3 normalMatrix;
uniform mat4 finalBonesMatrices[K_MAX_BONES];
uniform int useSkinning;

void main() {
  vec4 localPos = vec4(position, 1.0);
  vec3 localNormal = normal;
  vec3 localTangent = tangent;
  if (useSkinning != 0) {
    vec4 skinnedPos = vec4(0.0);
    vec3 skinnedNormal = vec3(0.0);
    vec3 skinnedTangent = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
      int b = boneIDs[i];
      if (b < 0 || b >= K_MAX_BONES) continue;
      mat4 boneM = finalBonesMatrices[b];
      mat3 boneM3 = mat3(boneM);
      skinnedPos += (boneM * vec4(position, 1.0)) * weights[i];
      skinnedNormal += (boneM3 * normal) * weights[i];
      skinnedTangent += (boneM3 * tangent) * weights[i];
    }
    // Unweighted vertices on a skinned mesh would otherwise collapse to the
    // origin; keep them in bind pose.
    if (skinnedPos.w > 0.0) {
      localPos = skinnedPos;
      localNormal = skinnedNormal;
      localTangent = skinnedTangent;
    }
  }
  vec4 world = model * localPos;
  gl_Position = camera.projection * camera.view * world;
  worldPos = world.xyz;
  worldNormal = normalMatrix * localNormal;
  worldTangent = normalMatrix * localTangent;
  vTexCoords = texCoords;
}
