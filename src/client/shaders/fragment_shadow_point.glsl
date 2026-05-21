#version 410 core
in vec3 fragWorldPos;

uniform vec3 lightPos;
uniform float pointFarPlane;

void main() {
  float d = length(fragWorldPos - lightPos);
  gl_FragDepth = clamp(d / pointFarPlane, 0.0, 1.0);
}
