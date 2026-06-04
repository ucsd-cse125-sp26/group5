#version 410 core
// Lightweight FXAA: 3x3 luma contrast detect, blend along the dominant edge.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;
uniform int fxaaEnabled;
// 0/1 = off; >=2 quantizes the HSV value of the final color into N levels.
uniform int postQuantizeLevels;
// Ordered (Bayer 4x4) dither amount in LSBs; 0 = off.
uniform float ditherStrength;

// Returns the 4x4 ordered-dither threshold in [0,1) for this pixel.
float bayer4(vec2 p) {
  int x = int(mod(p.x, 4.0));
  int y = int(mod(p.y, 4.0));
  int i = y * 4 + x;
  float m[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0,
                        1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
  return m[i] / 16.0;
}

vec3 applyDither(vec3 c) {
  if (ditherStrength <= 0.0) return c;
  float d = (bayer4(gl_FragCoord.xy) - 0.5) / 255.0;
  return c + d * ditherStrength;
}

const float kEdgeThreshold = 0.0625;
const float kMinEdgeContrast = 0.0312;
const float kSubpixelBlend = 0.75;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// HSV-V quantize: snap the brightness to N plateaus, preserve hue/saturation.
// Runs after FXAA so band edges stay crisp.
vec3 postQuantize(vec3 c) {
  if (postQuantizeLevels < 2) return c;
  float L = float(postQuantizeLevels);
  float v = max(c.r, max(c.g, c.b));
  if (v < 1e-5) return c;
  float qv = clamp(round(v * (L - 1.0)) / max(L - 1.0, 1.0), 0.0, 1.0);
  return c * (qv / v);
}

void main() {
  vec3 cM = texture(src, vUV).rgb;
  if (fxaaEnabled == 0) {
    FragColor = vec4(applyDither(postQuantize(cM)), 1.0);
    return;
  }

  vec2 texel = 1.0 / vec2(textureSize(src, 0));
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
    FragColor = vec4(applyDither(postQuantize(cM)), 1.0);
    return;
  }

  float horz = abs(lN + lS - 2.0 * lM);
  float vert = abs(lE + lW - 2.0 * lM);
  bool isHorz = horz >= vert;

  vec3 avg = isHorz ? (cE + cW) * 0.5 : (cN + cS) * 0.5;
  vec3 result = mix(cM, avg, kSubpixelBlend);
  FragColor = vec4(applyDither(postQuantize(result)), 1.0);
}
