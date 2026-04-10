#version 440 core
in vec3 colorFromVert;

out vec4 FragColor;

void main() {
  FragColor = vec4(colorFromVert, 1.0);
}
