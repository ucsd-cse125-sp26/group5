#version 410 core
in vec4 fragWorldPos;
flat in int lightIdx;

uniform vec3 lightPositions[K_MAX_POINT_LIGHTS];
uniform float pointFarPlane;

void main() {
  // Linear distance / pointFarPlane → [0,1] depth attachment.
  float d = length(fragWorldPos.xyz - lightPositions[lightIdx]);
  gl_FragDepth = clamp(d / pointFarPlane, 0.0, 1.0);
}
