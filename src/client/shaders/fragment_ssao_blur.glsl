#version 410 core
// 4x4 blur sized to the noise tile so rotation noise washes out. With
// bilateral != 0 the taps are weighted by view-space depth similarity so
// occlusion doesn't bleed across silhouettes (halo removal).
in vec2 vUV;
out float FragColor;

uniform sampler2D src;
uniform sampler2D gPosition;  // .rgb world pos, .a sky sentinel
uniform int bilateral;

layout(std140) uniform CameraBlock {
  mat4 view;
  mat4 projection;
  mat4 lightSpaceMatrix;
  vec3 viewPos;
  float pointFarPlane;
} camera;

float viewZ(vec2 uv) {
  return (camera.view * vec4(texture(gPosition, uv).rgb, 1.0)).z;
}

void main() {
  vec2 texel = 1.0 / vec2(textureSize(src, 0));
  if (bilateral == 0) {
    float result = 0.0;
    for (int x = -2; x < 2; ++x) {
      for (int y = -2; y < 2; ++y) {
        result += texture(src, vUV + vec2(float(x), float(y)) * texel).r;
      }
    }
    FragColor = result / 16.0;
    return;
  }

  vec4 cpos = texture(gPosition, vUV);
  float cz = (camera.view * vec4(cpos.rgb, 1.0)).z;
  float sum = 0.0;
  float wsum = 0.0;
  for (int x = -2; x < 2; ++x) {
    for (int y = -2; y < 2; ++y) {
      vec2 uv = vUV + vec2(float(x), float(y)) * texel;
      vec4 p = texture(gPosition, uv);
      // Depth-similarity weight; scale-adaptive (relative to center depth).
      float w = max(0.0, 1.0 - abs(viewZ(uv) - cz) / (0.1 * abs(cz) + 0.5));
      // Don't pull sky samples into real geometry (or vice-versa).
      if (p.a < 0.5 != cpos.a < 0.5) w = 0.0;
      sum += texture(src, uv).r * w;
      wsum += w;
    }
  }
  FragColor = wsum > 0.0 ? sum / wsum : texture(src, vUV).r;
}
