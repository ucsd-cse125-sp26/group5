#version 410 core
in vec4 fragWorldPos;
flat in int lightIdx;

uniform vec3 lightPositions[4];
uniform float pointFarPlane;

void main() {
  // Linear distance from the light, normalized into [0, 1] for the depth
  // attachment. The matching read in the main shader multiplies back by
  // pointFarPlane.
  float d = length(fragWorldPos.xyz - lightPositions[lightIdx]);
  gl_FragDepth = clamp(d / pointFarPlane, 0.0, 1.0);
}
