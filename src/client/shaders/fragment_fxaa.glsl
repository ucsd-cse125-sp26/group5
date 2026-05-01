#version 410 core
// Lightweight FXAA. Detects luma contrast in a 3x3 neighborhood; if above a
// threshold, finds the dominant edge direction and blends along it.
// Approximate but cheap — enough to mask the aliasing we lost when MSAA
// went away in phase 2.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;

const float kEdgeThreshold = 0.0625;       // ~1/16 of luma range
const float kMinEdgeContrast = 0.0312;     // baseline noise floor
const float kSubpixelBlend = 0.75;          // strength of the final blend

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

  // Below the noise floor: no edge — passthrough.
  if (range < max(kMinEdgeContrast, lMax * kEdgeThreshold)) {
    FragColor = vec4(cM, 1.0);
    return;
  }

  // Pick edge direction by comparing horizontal vs vertical luma gradients.
  float horz = abs(lN + lS - 2.0 * lM);
  float vert = abs(lE + lW - 2.0 * lM);
  bool isHorz = horz >= vert;

  // Average two samples along the edge (perpendicular to the gradient).
  vec3 avg = isHorz ? (cE + cW) * 0.5 : (cN + cS) * 0.5;
  vec3 result = mix(cM, avg, kSubpixelBlend);
  FragColor = vec4(result, 1.0);
}
