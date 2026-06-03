#version 410 core
// HDR + bloom, exposure tonemap, gamma to LDR (no GL_FRAMEBUFFER_SRGB).
// Optionally desaturates fragments outside the player's ColorBoundingBox so
// world regions the player hasn't "restored" read as monochrome.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D hdrColor;
uniform sampler2D bloomColor;
uniform sampler2D gPosition;  // .rgb worldPos, .a sky sentinel (0 = sky)
uniform float exposure;
uniform float bloomStrength;

// 0 = no effect; the strength is folded into the desaturation lerp.
uniform float colorRestorationStrength;
uniform float colorRestorationEdgeWidth;
uniform vec3 colorRestorationMin;
uniform vec3 colorRestorationMax;

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
  vec3 hdr = texture(hdrColor, vUV).rgb;
  vec3 bloom = texture(bloomColor, vUV).rgb;
  hdr += bloom * bloomStrength;
  vec3 mapped = vec3(1.0) - exp(-hdr * exposure);
  mapped = pow(mapped, vec3(1.0 / 2.2));

  if (colorRestorationStrength > 0.0) {
    vec4 pos = texture(gPosition, vUV);
    // Sky (pos.a == 0) keeps its color regardless of the box — desaturating
    // the cubemap is jarring and not what "restoration" means here.
    if (pos.a > 0.5) {
      float d = aabbSignedDistance(pos.rgb, colorRestorationMin,
                                   colorRestorationMax);
      float edge = max(colorRestorationEdgeWidth, 1e-4);
      float outsideAmt = smoothstep(0.0, edge, d);
      if (tangramAlwaysColorEnabled > 0.5) {
        float dTangram = aabbSignedDistance(pos.rgb, tangramAlwaysColorMin,
                                            tangramAlwaysColorMax);
        outsideAmt *= smoothstep(0.0, edge, dTangram);
      }
      float gray = dot(mapped, vec3(0.299, 0.587, 0.114));
      mapped = mix(mapped, vec3(gray),
                   outsideAmt * colorRestorationStrength);
    }
  }

  FragColor = vec4(mapped, 1.0);
}
