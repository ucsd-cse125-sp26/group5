#version 410 core
out vec4 FragColor;

in vec3 texCoords;
uniform samplerCube cubemap;

// 0/1 = off; >=2 snaps the HSV value to N plateaus, preserving hue/saturation.
uniform int skyboxQuantizeLevels;
// Palette quantization (CPU-built via k-means over sampled cubemap pixels).
// paletteSize == 0 → off; otherwise the sampled color is snapped to the
// closest palette entry (linear-space Euclidean nearest).
uniform int skyboxPaletteSize;
uniform vec3 skyboxPalette[K_MAX_PALETTE_COLORS];

vec3 skyboxQuantize(vec3 c) {
  if (skyboxQuantizeLevels < 2) return c;
  float L = float(skyboxQuantizeLevels);
  float v = max(c.r, max(c.g, c.b));
  if (v < 1e-5) return c;
  float qv = clamp(round(v * (L - 1.0)) / max(L - 1.0, 1.0), 0.0, 1.0);
  return c * (qv / v);
}

vec3 skyboxPaletteSnap(vec3 c) {
  if (skyboxPaletteSize <= 0) return c;
  vec3 best = skyboxPalette[0];
  float bestD = dot(c - best, c - best);
  for (int i = 1; i < skyboxPaletteSize; ++i) {
    vec3 p = skyboxPalette[i];
    float d = dot(c - p, c - p);
    if (d < bestD) {
      bestD = d;
      best = p;
    }
  }
  return best;
}

void main()
{
  vec3 c = texture(cubemap, texCoords).rgb;
  // Brightness quantize first, then snap to palette — mirrors the gbuffer
  // order so the same setting pair produces the same colors on geometry.
  FragColor = vec4(skyboxPaletteSnap(skyboxQuantize(c)), 1.0);
}
