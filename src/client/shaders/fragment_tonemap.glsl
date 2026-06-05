#version 410 core
// HDR + bloom, exposure tonemap, gamma to LDR (no GL_FRAMEBUFFER_SRGB).
// Optionally desaturates fragments outside the player's ColorBoundingBox so
// world regions the player hasn't "restored" read as monochrome.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D hdrColor;
uniform sampler2D bloomColor;
uniform sampler2D gPosition;  // .rgb worldPos, .a sky sentinel (0 = sky)
uniform sampler2D gAlbedo;    // .a = 1 marks meshes exempt from restoration
uniform float exposure;
uniform float bloomStrength;
// 0 = exponential (current), 1 = ACES (Narkowicz), 2 = AgX. Each returns a
// linear value so the shared gamma curve below still applies uniformly.
uniform int tonemapMode;

vec3 acesTonemap(vec3 x) {
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 agxContrastApprox(vec3 x) {
  vec3 x2 = x * x;
  vec3 x4 = x2 * x2;
  return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x +
         0.4298 * x2 + 0.1191 * x - 0.00232;
}

// three.js-style minimal AgX; returns linear (inverse gamma at the end).
vec3 agxTonemap(vec3 color) {
  const mat3 inset =
      mat3(0.856627153315983, 0.137318972929847, 0.11189821299995,
           0.0951212405381588, 0.761241990602591, 0.0767994186031903,
           0.0482516061458583, 0.101439036467562, 0.811302368396859);
  const mat3 outset =
      mat3(1.1271005818144368, -0.1413297634984383, -0.14132976349843826,
           -0.11060664309660323, 1.157823702216272, -0.11060664309660294,
           -0.016493938717834573, -0.016493938717834257, 1.2519364065950405);
  const float minEv = -12.47393;
  const float maxEv = 4.026069;
  color = inset * color;
  color = max(color, 1e-10);
  color = log2(color);
  color = (color - minEv) / (maxEv - minEv);
  color = clamp(color, 0.0, 1.0);
  color = agxContrastApprox(color);
  color = outset * color;
  return pow(max(vec3(0.0), color), vec3(2.2));
}

// 0 = no effect; the strength is folded into the desaturation lerp.
uniform float colorRestorationStrength;
uniform float colorRestorationEdgeWidth;
uniform vec3 colorRestorationMin;
uniform vec3 colorRestorationMax;
// How strongly colored point lights resist the desaturation (0 = off, 1 = full).
uniform float colorRestorationLightStrength;

// Tangram play area stays in full color regardless of restoration progress.
uniform float tangramAlwaysColorEnabled;
uniform vec3 tangramAlwaysColorMin;
uniform vec3 tangramAlwaysColorMax;
// Signed distance from p to the AABB [mn, mx]. Negative inside, positive
// outside; smooth across the edge so we don't get a hard ring.
float aabbSignedDistance(vec3 p, vec3 mn, vec3 mx) {
  vec3 q = max(mn - p, p - mx);
  float outside = length(max(q, vec3(0.0)));
  float inside = min(max(q.x, max(q.y, q.z)), 0.0);
  return outside + inside;
}

void main() {
  vec4 hdrSample = texture(hdrColor, vUV);
  vec3 hdr = hdrSample.rgb;
  // .a carries how much of this pixel is lit by the (colored) point lights.
  float coloredLightKeep = hdrSample.a;
  vec3 bloom = texture(bloomColor, vUV).rgb;
  hdr += bloom * bloomStrength;
  vec3 exposed = hdr * exposure;
  vec3 mapped;
  if (tonemapMode == 1) {
    mapped = acesTonemap(exposed);
  } else if (tonemapMode == 2) {
    mapped = agxTonemap(exposed);
  } else {
    mapped = vec3(1.0) - exp(-exposed);
  }
  mapped = pow(mapped, vec3(1.0 / 2.2));

  if (colorRestorationStrength > 0.0) {
    vec4 pos = texture(gPosition, vUV);
    // gAlbedo.a == 1 marks meshes (fragments, game stage) that opt out of the
    // recolor effect and always stay in full color.
    float exemptFromRecolor = texture(gAlbedo, vUV).a;
    // Sky (pos.a == 0) keeps its color regardless of the box — desaturating
    // the cubemap is jarring and not what "restoration" means here.
    if (pos.a > 0.5 && exemptFromRecolor < 0.5) {
      float d = aabbSignedDistance(pos.rgb, colorRestorationMin,
                                   colorRestorationMax);
      float edge = max(colorRestorationEdgeWidth, 1e-4);
      float outsideAmt = smoothstep(0.0, edge, d);
      if (tangramAlwaysColorEnabled > 0.5) {
        float dTangram = aabbSignedDistance(pos.rgb, tangramAlwaysColorMin,
                                            tangramAlwaysColorMax);
        outsideAmt *= smoothstep(0.0, edge, dTangram);
      }
      // Keep the colored point light's illuminated area in color even out here
      // (scaled by the configurable strength; 0 = off).
      outsideAmt *= 1.0 - clamp(coloredLightKeep * colorRestorationLightStrength,
                                0.0, 1.0);
      float gray = dot(mapped, vec3(0.299, 0.587, 0.114));
      mapped = mix(mapped, vec3(gray),
                   outsideAmt * colorRestorationStrength);
    }
  }

  FragColor = vec4(mapped, 1.0);
}
