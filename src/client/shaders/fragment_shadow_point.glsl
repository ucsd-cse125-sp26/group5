#version 410 core
in vec4 fragWorldPos;

uniform vec3 lightPosition;
uniform float pointFarPlane;

void main() {
  float d = length(fragWorldPos.xyz - lightPosition);
  gl_FragDepth = clamp(d / pointFarPlane, 0.0, 1.0);
}
