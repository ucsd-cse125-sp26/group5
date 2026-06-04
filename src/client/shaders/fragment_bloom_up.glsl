#version 410 core
// 3x3 tent upsample filter. Reads the smaller bloom level; the renderer blends
// the result additively into the next-larger level (GL_ONE, GL_ONE).
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;
uniform vec2 srcTexel;       // 1 / size of the source (smaller) texture
uniform float filterRadius;  // tent spread in source texels

void main() {
  vec2 r = srcTexel * filterRadius;
  vec3 a = texture(src, vUV + vec2(-1.0, 1.0) * r).rgb;
  vec3 b = texture(src, vUV + vec2(0.0, 1.0) * r).rgb;
  vec3 c = texture(src, vUV + vec2(1.0, 1.0) * r).rgb;
  vec3 d = texture(src, vUV + vec2(-1.0, 0.0) * r).rgb;
  vec3 e = texture(src, vUV).rgb;
  vec3 f = texture(src, vUV + vec2(1.0, 0.0) * r).rgb;
  vec3 g = texture(src, vUV + vec2(-1.0, -1.0) * r).rgb;
  vec3 h = texture(src, vUV + vec2(0.0, -1.0) * r).rgb;
  vec3 i = texture(src, vUV + vec2(1.0, -1.0) * r).rgb;

  vec3 col = e * 4.0;
  col += (b + d + f + h) * 2.0;
  col += (a + c + g + i);
  col *= (1.0 / 16.0);
  FragColor = vec4(col, 1.0);
}
