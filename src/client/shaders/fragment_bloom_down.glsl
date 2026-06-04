#version 410 core
// 13-tap downsample filter (Call of Duty / Jimenez "next-gen post"). Reads the
// previous (larger) bloom level; writes the next (half-size) one.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;
uniform vec2 srcTexel;  // 1 / size of the source texture

void main() {
  vec2 t = srcTexel;
  vec3 a = texture(src, vUV + vec2(-2.0, 2.0) * t).rgb;
  vec3 b = texture(src, vUV + vec2(0.0, 2.0) * t).rgb;
  vec3 c = texture(src, vUV + vec2(2.0, 2.0) * t).rgb;
  vec3 d = texture(src, vUV + vec2(-2.0, 0.0) * t).rgb;
  vec3 e = texture(src, vUV).rgb;
  vec3 f = texture(src, vUV + vec2(2.0, 0.0) * t).rgb;
  vec3 g = texture(src, vUV + vec2(-2.0, -2.0) * t).rgb;
  vec3 h = texture(src, vUV + vec2(0.0, -2.0) * t).rgb;
  vec3 i = texture(src, vUV + vec2(2.0, -2.0) * t).rgb;
  vec3 j = texture(src, vUV + vec2(-1.0, 1.0) * t).rgb;
  vec3 k = texture(src, vUV + vec2(1.0, 1.0) * t).rgb;
  vec3 l = texture(src, vUV + vec2(-1.0, -1.0) * t).rgb;
  vec3 m = texture(src, vUV + vec2(1.0, -1.0) * t).rgb;

  vec3 col = e * 0.125;
  col += (a + c + g + i) * 0.03125;
  col += (b + d + f + h) * 0.0625;
  col += (j + k + l + m) * 0.125;
  FragColor = vec4(max(col, vec3(0.0)), 1.0);
}
