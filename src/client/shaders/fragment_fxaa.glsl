#version 410 core
// Lightweight FXAA: 3x3 luma contrast detect, blend along the dominant edge.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;

const float kEdgeThreshold = 0.0625;
const float kMinEdgeContrast = 0.0312;
const float kSubpixelBlend = 0.75;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec2 texel = 1.0 / vec2(textureSize(src, 0));

  vec3 cM = texture(src, vUV).rgb;
  vec3 cN = texture(src, vUV + vec2(0.0,  texel.y)).rgb;
  vec3 cS = texture(src, vUV + vec2(0.0, -texel.y)).rgb;
  vec3 cE = texture(src, vUV + vec2( texel.x, 0.0)).rgb;
  vec3 cW = texture(src, vUV + vec2(-texel.x, 0.0)).rgb;

  float lM = luma(cM);
  float lN = luma(cN);
  float lS = luma(cS);
  float lE = luma(cE);
  float lW = luma(cW);

  float lMin = min(lM, min(min(lN, lS), min(lE, lW)));
  float lMax = max(lM, max(max(lN, lS), max(lE, lW)));
  float range = lMax - lMin;

  if (range < max(kMinEdgeContrast, lMax * kEdgeThreshold)) {
    FragColor = vec4(cM, 1.0);
    return;
  }

  float horz = abs(lN + lS - 2.0 * lM);
  float vert = abs(lE + lW - 2.0 * lM);
  bool isHorz = horz >= vert;

  vec3 avg = isHorz ? (cE + cW) * 0.5 : (cN + cS) * 0.5;
  vec3 result = mix(cM, avg, kSubpixelBlend);
  FragColor = vec4(result, 1.0);
}
