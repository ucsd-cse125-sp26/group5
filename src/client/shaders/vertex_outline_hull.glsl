#version 410 core
layout(location = 0) in vec3 position;
// Hull expansion uses the smoothed (per-vertex averaged) normal so cube
// corners and other hard-edge meshes don't gap open at large thicknesses.
// Equal to the flat normal for artist-meshed assets.
layout(location = 3) in vec3 smoothedNormal;

layout(std140) uniform CameraBlock {
  mat4 view;
  mat4 projection;
  mat4 lightSpaceMatrix;
  vec3 viewPos;
  float pointFarPlane;
} camera;

uniform mat4 model;
uniform mat3 normalMatrix;
uniform float outlineThickness;     // world units (or screen factor)
uniform int outlineScreenSpace;     // 0 = world, 1 = screen-constant

void main() {
  vec3 worldNormal = normalize(normalMatrix * smoothedNormal);
  vec4 worldPos = model * vec4(position, 1.0);
  if (outlineScreenSpace == 1) {
    vec4 clip = camera.projection * camera.view * worldPos;
    vec4 clipNormal = camera.projection * camera.view * vec4(worldNormal, 0.0);
    vec2 dir = length(clipNormal.xy) > 1e-6
        ? normalize(clipNormal.xy)
        : vec2(0.0);
    clip.xy += dir * outlineThickness * clip.w;
    gl_Position = clip;
  } else {
    worldPos.xyz += worldNormal * outlineThickness;
    gl_Position = camera.projection * camera.view * worldPos;
  }
}
