#version 410 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;

void main() {
  FragColor = vec4(texture(src, vUV).rgb, 1.0);
}
