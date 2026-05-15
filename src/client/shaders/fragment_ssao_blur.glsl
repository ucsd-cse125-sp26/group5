#version 410 core
// 4x4 box blur sized to the noise tile so rotation noise washes out.
in vec2 vUV;
out float FragColor;

uniform sampler2D src;

void main() {
  vec2 texel = 1.0 / vec2(textureSize(src, 0));
  float result = 0.0;
  for (int x = -2; x < 2; ++x) {
    for (int y = -2; y < 2; ++y) {
      vec2 offset = vec2(float(x), float(y)) * texel;
      result += texture(src, vUV + offset).r;
    }
  }
  FragColor = result / 16.0;
}
