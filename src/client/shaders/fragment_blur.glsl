#version 410 core
// Separable 5-tap Gaussian (learnopengl weights). Ping-pong H/V draws.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;
uniform bool horizontal;

const float weights[5] = float[](
    0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
  vec2 texel = 1.0 / vec2(textureSize(src, 0));
  vec3 result = texture(src, vUV).rgb * weights[0];
  if (horizontal) {
    for (int i = 1; i < 5; ++i) {
      result += texture(src, vUV + vec2(texel.x * i, 0.0)).rgb * weights[i];
      result += texture(src, vUV - vec2(texel.x * i, 0.0)).rgb * weights[i];
    }
  } else {
    for (int i = 1; i < 5; ++i) {
      result += texture(src, vUV + vec2(0.0, texel.y * i)).rgb * weights[i];
      result += texture(src, vUV - vec2(0.0, texel.y * i)).rgb * weights[i];
    }
  }
  FragColor = vec4(result, 1.0);
}
