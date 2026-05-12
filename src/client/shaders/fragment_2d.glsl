#version 410 core

in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
  FragColor = texture(uTexture, vUV) * vColor;
}
