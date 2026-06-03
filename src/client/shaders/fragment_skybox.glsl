#version 410 core
out vec4 FragColor;

in vec3 texCoords;
uniform samplerCube cubemap;

// When 1, the sample direction's Z is negated, flipping the cubemap
// upside-down across the Z=0 plane. Used for the night/winter scene.
uniform int skyboxFlipZ;
// 0 = off, 1 = max. Bayer 4×4 ordered-dither offset applied to brightness
// (before quantize) and to all channels (before palette snap), softening the
// hard plateau / Voronoi region edges that posterization produces.
uniform float skyboxSoftEdge;
// 0/1 = off; >=2 snaps the HSV value to N plateaus, preserving hue/saturation.
uniform int skyboxQuantizeLevels;
// Palette quantization (CPU-built via k-means over sampled cubemap pixels).
// paletteSize == 0 → off; otherwise the sampled color is snapped to the
// closest palette entry (linear-space Euclidean nearest).
uniform int skyboxPaletteSize;
uniform vec3 skyboxPalette[K_MAX_PALETTE_COLORS];

// Bayer 4×4 ordered-dither value in [-0.5, 0.5], keyed off pixel coords. Same
// value every frame at a given pixel — no temporal flicker.
float bayer4() {
  const float m[16] = float[16](
       0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
      12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
       3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
      15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0);
  int x = int(gl_FragCoord.x) & 3;
  int y = int(gl_FragCoord.y) & 3;
  return m[y * 4 + x] - 0.5;
}

vec3 skyboxQuantize(vec3 c) {
  if (skyboxQuantizeLevels < 2) return c;
  float L = float(skyboxQuantizeLevels);
  float v = max(c.r, max(c.g, c.b));
  if (v < 1e-5) return c;
  // Dither offset: at strength 1, ±0.5 plateau widths — enough to push some
  // pixels at a boundary one step over and produce a stippled gradient.
  float vIn = v + bayer4() * (skyboxSoftEdge / max(L - 1.0, 1.0));
  vIn = clamp(vIn, 0.0, 1.0);
  float qv = clamp(round(vIn * (L - 1.0)) / max(L - 1.0, 1.0), 0.0, 1.0);
  return c * (qv / v);
}

vec3 skyboxPaletteSnap(vec3 c) {
  if (skyboxPaletteSize <= 0) return c;
  // Push the input color along all three axes so border pixels straddle
  // adjacent palette Voronoi regions, breaking hard boundaries into stipple.
  // 0.15 is empirical — small enough that interior regions still snap clean.
  vec3 d = vec3(bayer4()) * (skyboxSoftEdge * 0.15);
  vec3 cIn = c + d;
  vec3 best = skyboxPalette[0];
  float bestD = dot(cIn - best, cIn - best);
  for (int i = 1; i < skyboxPaletteSize; ++i) {
    vec3 p = skyboxPalette[i];
    float dd = dot(cIn - p, cIn - p);
    if (dd < bestD) {
      bestD = dd;
      best = p;
    }
  }
  return best;
}

void main()
{
  // Cubemap data is Y-up; the kCubemapToGame view rotation maps cubemap Y to
  // game Z. So a vertical (game Z) flip is a negate-Y in the sample direction,
  // not negate-Z.
  vec3 dir = skyboxFlipZ != 0
      ? vec3(texCoords.x, -texCoords.y, texCoords.z)
      : texCoords;
  vec3 c = texture(cubemap, dir).rgb;
  // Brightness quantize first, then snap to palette — mirrors the gbuffer
  // order so the same setting pair produces the same colors on geometry.
  FragColor = vec4(skyboxPaletteSnap(skyboxQuantize(c)), 1.0);
}
