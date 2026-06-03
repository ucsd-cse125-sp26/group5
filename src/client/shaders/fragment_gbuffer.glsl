#version 410 core
in vec3 worldPos;
in vec3 worldNormal;
in vec3 worldTangent;
in vec2 vTexCoords;

// gPosition: .rgb world pos, .a=1 sentinel ("real geometry, not sky").
// gNormal:   .rgb world normal, .a shininess.
// gAlbedo/gSpecular/gEmissive: .rgb sampled material; .a unused.
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec4 gSpecular;
layout(location = 4) out vec4 gEmissive;

struct Material {
  sampler2D ambient;
  sampler2D diffuse;
  sampler2D specular;
  sampler2D emissive;
  sampler2D normal;
  float shininess;
};
uniform Material material;

// 0 = off; >=2 posterizes each RGB channel to N evenly-spaced levels.
uniform int textureQuantizeLevels;

// Rainbow effect: when rainbowStrength > 0 the albedo (and a soft emissive
// glow) is lerped toward a hue that scrolls with rainbowTime and varies along
// world position, so the surface reads as shifting rainbow colors.
uniform float rainbowStrength;
uniform float rainbowTime;

// 1 = this mesh is exempt from color-restoration desaturation; written to
// gAlbedo.a (otherwise unused) so the tonemap pass can keep it in full color.
uniform int alwaysColor;

vec3 hsv2rgb(vec3 c) {
  vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
  return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

// Palette quantization (CPU-built via k-means over sampled diffuse pixels).
// paletteSize == 0 → off; otherwise the gbuffer pass snaps the sampled
// albedo to the closest palette entry (linear-space Euclidean nearest).
uniform int paletteSize;
uniform vec3 palette[K_MAX_PALETTE_COLORS];

vec3 quantizeRGB(vec3 c) {
  if (textureQuantizeLevels < 2) return c;
  // Quantize the HSV value (max channel) and rescale all channels by the
  // same factor. Per-channel posterization snaps each channel to its own
  // plateau, which shifts hues toward whichever primary dominates and
  // pumps saturation; this scheme keeps hue and saturation untouched and
  // only discretizes brightness.
  float L = float(textureQuantizeLevels);
  float v = max(c.r, max(c.g, c.b));
  if (v < 1e-5) return c;
  // round() centers each plateau in its snap range; floor would bias the
  // whole image upward in brightness.
  float qv = clamp(round(v * (L - 1.0)) / max(L - 1.0, 1.0), 0.0, 1.0);
  return c * (qv / v);
}

// Linear-RGB nearest-neighbour lookup against the global palette. Only the
// albedo is quantized this way — specular/emissive stay on the uniform
// brightness quantizer above so masks/glow don't snap to weird palette
// entries derived purely from diffuse pixels.
vec3 paletteSnap(vec3 c) {
  if (paletteSize <= 0) return c;
  vec3 best = palette[0];
  float bestD = dot(c - best, c - best);
  for (int i = 1; i < paletteSize; ++i) {
    vec3 p = palette[i];
    float d = dot(c - p, c - p);
    if (d < bestD) {
      bestD = d;
      best = p;
    }
  }
  return best;
}

void main() {
  vec4 diffuse = texture(material.diffuse, vTexCoords);
  if (diffuse.a < 0.5) discard;

  // Gram-Schmidt re-orthogonalize T against N to absorb interpolation drift,
  // then derive B by cross to fix handedness regardless of attribute sign.
  vec3 N = normalize(worldNormal);
  vec3 T = normalize(worldTangent - dot(worldTangent, N) * N);
  vec3 B = cross(N, T);
  mat3 TBN = mat3(T, B, N);

  vec3 tn = texture(material.normal, vTexCoords).rgb * 2.0 - 1.0;
  vec3 norm = normalize(TBN * tn);

  vec3 albedo = paletteSnap(quantizeRGB(diffuse.rgb));
  vec3 emissive = texture(material.emissive, vTexCoords).rgb;

  if (rainbowStrength > 0.0) {
    // Hue scrolls in time and drifts along the surface so the whole mesh is a
    // moving rainbow rather than one flat shifting color.
    float hue = fract(rainbowTime * 0.12 + worldPos.z * 0.15 +
                      worldPos.x * 0.05 + worldPos.y * 0.05);
    vec3 rainbow = hsv2rgb(vec3(hue, 0.9, 1.0));
    albedo = mix(albedo, rainbow, rainbowStrength);
    // Gentle emissive so the fragment glows its current color (kept under the
    // bloom threshold so it shimmers without blowing out).
    emissive = mix(emissive, rainbow * 0.4, rainbowStrength);
  }

  gPosition = vec4(worldPos, 1.0);
  gNormal = vec4(norm, material.shininess);
  // gAlbedo.a carries the "exempt from color restoration" flag for the tonemap.
  gAlbedo = vec4(albedo, alwaysColor != 0 ? 1.0 : 0.0);
  gSpecular = vec4(quantizeRGB(texture(material.specular, vTexCoords).rgb), 1.0);
  // Skip the brightness quantizer for emissive: dim glows (e.g. 0.05) round
  // to 0 under any reasonable level count and the surface goes dark.
  gEmissive = vec4(emissive, 1.0);
}
