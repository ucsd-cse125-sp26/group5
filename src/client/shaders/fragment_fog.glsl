#version 410 core
// Volumetric "barrier fog" — march pass. Produces the fog contribution (a
// premultiplied color + transmittance) for each active section barrier; a
// separate composite pass blends it over the scene. All barriers share one
// tint = the current season.
//
// Each barrier box is ray-marched separately, then combined by NEAREST depth (a
// soft, depth-weighted pick): a nearer wall occludes the ones behind it, so
// barriers stacked along the view read as one wall at one depth instead of
// several translucent planes, and the fog is never denser than a single barrier
// (no additive "double fog"). Density is multi-octave (FBM) with a jittered
// start to avoid banding, plus directional in-scattering so the mist reads as
// lit volume. The march is clamped to scene depth (gPosition) for correct
// occlusion, and runs at a reduced resolution (see fogScale) because the signal
// is low-frequency — the big perf lever.
//
// A frustum-aligned froxel volume (Wronski-style) would be the "AAA" approach
// but needs compute shaders (GL 4.3+); this engine is GL 4.1 core (macOS), and
// the fog is confined to thin walls, so per-box ray-marching matches the look.
in vec2 vUV;
// rgb = premultiplied fog color, a = transmittance (1 = clear). The composite
// pass does: scene * a + rgb.
out vec4 FragColor;

uniform sampler2D gPosition;  // .rgb world pos, .a == 0 => sky / unwritten

uniform mat4 uInvViewProj;  // reconstruct sky-pixel rays (no world pos there)
uniform vec3 uCamPos;
uniform float uFarPlane;

uniform float uTime;
uniform float uDensity;        // opacity accrued per world-unit of ray in fog
uniform float uHeightFalloff;  // 1/units: mist thins with height above fade start
uniform float uHeightScale;    // scales fog vertical reach vs the collision box
uniform float uFadeFraction;   // fraction of fog height at full density before fade
uniform float uWidth;          // world-space distance the mist fades in over
uniform float uNoiseScale;     // world-space frequency of the drifting mist
uniform float uNoiseSpeed;     // scroll speed of the mist
uniform float uNoiseStrength;  // 0 = uniform, 1 = fully noise-modulated
uniform float uSwirl;          // domain-warp amount: billowing/swirling mist
uniform float uMistiness;      // carves box faces into wisps (0 = hard box)
uniform int uFloorDetect;      // 1 = anchor the fade to landscape ground (gPos)
uniform int uSteps;            // ray-march samples across the fog span

uniform vec3 uFogColor;          // single season tint shared by every barrier
uniform vec3 uSunDir;            // normalized world-space direction toward the sun
uniform vec3 uScatterColor;      // in-scattered sunlight color
uniform float uScatterStrength;  // 0 = unlit (flat tint), higher = stronger halo
uniform float uScatterAnisotropy;  // Henyey-Greenstein g (forward-scatter bias)
uniform int uLighting;       // 1 = self-shadowed 3D shading, 0 = flat tint
uniform float uFogAmbient;   // shadow-floor brightness (lower = more 3D contrast)

uniform int uBoxCount;
uniform vec3 uBoxCenter[K_MAX_FOG_BOXES];
uniform vec3 uBoxHalf[K_MAX_FOG_BOXES];

const float PI = 3.14159265359;
const int MAX_STEPS = 32;  // hard cap so the loop bound stays constant

float hash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
  vec3 i = floor(p);
  vec3 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
  float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
  float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
  float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
  float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
  float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
  float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
  float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

// 2-octave fractal noise, normalized to ~[0,1]. (2 octaves is the perf/quality
// sweet spot here; a third octave roughly doubled cost for little gain.)
float fbm(vec3 p) {
  float n = 0.5 * valueNoise(p) + 0.25 * valueNoise(p * 2.02);
  return n / 0.75;
}

// Domain-warped FBM: a low-frequency flow field displaces the sample so the
// noise billows and curls like real drifting mist instead of reading as static
// lumps. Costs three extra noise taps, but only when uSwirl > 0.
float mistFbm(vec3 p) {
  if (uSwirl > 0.001) {
    vec3 w = vec3(valueNoise(p * 0.5), valueNoise(p * 0.5 + vec3(17.3, 9.1, 31.7)),
                  valueNoise(p * 0.5 + vec3(5.2, 23.4, 11.9)));
    p += uSwirl * (w * 2.0 - 1.0);
  }
  return fbm(p);
}

// Interleaved-gradient noise: static per-pixel dither to jitter the march start
// so a small step count doesn't show slab banding.
float ign(vec2 p) {
  return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

float henyeyGreenstein(float cosT, float g) {
  float g2 = g * g;
  float denom = 1.0 + g2 - 2.0 * g * cosT;
  return (1.0 - g2) / (4.0 * PI * pow(max(denom, 1e-4), 1.5));
}

// Branchless ray/AABB slab test. Returns (tNear, tFar); a miss has tFar < tNear.
vec2 intersectBox(vec3 ro, vec3 rd, vec3 mn, vec3 mx) {
  vec3 inv = 1.0 / rd;  // inf for axis-aligned rays is handled by min/max below
  vec3 t0 = (mn - ro) * inv;
  vec3 t1 = (mx - ro) * inv;
  vec3 tsmall = min(t0, t1);
  vec3 tbig = max(t0, t1);
  return vec2(max(max(tsmall.x, tsmall.y), tsmall.z),
              min(min(tbig.x, tbig.y), tbig.z));
}

void main() {
  vec4 posSample = texture(gPosition, vUV);
  vec3 rayDir;
  float maxDist;
  if (posSample.a > 0.0) {
    vec3 d = posSample.rgb - uCamPos;  // world-space geometry hit
    maxDist = length(d);
    rayDir = d / max(maxDist, 1e-4);
  } else {
    // Sky: reconstruct the world-space ray through this pixel at the far plane.
    vec4 clip = vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    rayDir = normalize(world.xyz / world.w - uCamPos);
    maxDist = uFarPlane;
  }

  if (uBoxCount <= 0 || uDensity <= 0.0) {
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);  // no fog: transmittance = 1
    return;
  }

  // Floor detection: the scene geometry behind the fog (gPosition) is the
  // landscape under the mist, so its world Z anchors the height fade and the
  // fog hugs the terrain instead of a flat z = 0. Sky pixels (no geometry) and
  // the toggle-off path fall back to each barrier's own center Z.
  bool haveFloor = uFloorDetect != 0 && posSample.a > 0.0;
  float sceneFloorZ = posSample.z;

  vec3 sunDir = normalize(uSunDir);
  // Flat fallback tint (used when lighting is off): season color + a sun halo.
  float phase = henyeyGreenstein(dot(rayDir, sunDir),
                                 clamp(uScatterAnisotropy, 0.0, 0.95));
  vec3 litFog = uFogColor + uScatterColor * (uScatterStrength * phase);

  int steps = clamp(uSteps, 1, MAX_STEPS);
  float jitter = ign(gl_FragCoord.xy);
  vec3 scroll = vec3(0.07, 0.11, 0.05) * (uTime * uNoiseSpeed);
  // Self-shadow march step toward the sun, ~a quarter of a noise feature wide.
  float lightStep = 0.25 / max(uNoiseScale, 0.001);

  // Nearest barrier entry along the ray (so a nearer wall occludes farther ones
  // in the combine below).
  float minTN = 1e30;
  for (int b = 0; b < uBoxCount && b < K_MAX_FOG_BOXES; ++b) {
    vec3 halfExt = uBoxHalf[b];
    halfExt.z *= uHeightScale;
    halfExt.xy += vec2(uWidth);  // widened footprint for the soft fade-in
    vec2 t = intersectBox(uCamPos, rayDir, uBoxCenter[b] - halfExt,
                          uBoxCenter[b] + halfExt);
    if (min(t.y, maxDist) > max(t.x, 0.0)) minTN = min(minTN, max(t.x, 0.0));
  }
  if (minTN > 1e29) {
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);  // no barrier on this ray
    return;
  }

  // March each barrier and blend toward the NEAREST (weight falls off over
  // kBlend world units): a nearer wall occludes the ones behind it, so stacked
  // barriers read as one wall at one depth, never denser than a single barrier.
  const float kBlend = 4.0;
  vec3 cAccum = vec3(0.0);  // premultiplied, nearest-weighted fog color
  float aAccum = 0.0;       // nearest-weighted opacity
  float wsum = 0.0;
  for (int b = 0; b < uBoxCount && b < K_MAX_FOG_BOXES; ++b) {
    vec3 center = uBoxCenter[b];
    vec3 origHalf = uBoxHalf[b];          // the gameplay wall footprint
    vec3 halfExt = origHalf;
    halfExt.z *= uHeightScale;            // let the mist rise above the wall
    halfExt.xy += vec2(uWidth);           // ...and fade in over a wider footprint
    vec3 mn = center - halfExt;
    vec3 mx = center + halfExt;
    vec2 t = intersectBox(uCamPos, rayDir, mn, mx);
    float tN = max(t.x, 0.0);
    float tF = min(t.y, maxDist);
    if (tF <= tN) continue;  // miss, or fully behind nearer geometry

    // How far noise may carve the box surface inward, scaled by the wall's
    // thinnest half-extent (the original, not the widened footprint) so the
    // wispy carving behaves the same regardless of the fade-in width.
    float carve = uMistiness * min(origHalf.x, min(origHalf.y, halfExt.z));
    // Full density up to this height above the floor; mist thins above it.
    float fadeStart = uFadeFraction * halfExt.z;
    // Ground level: detected landscape Z (clamped into the box) or the barrier
    // center when detection is off / the backdrop is sky.
    float floorZ = haveFloor ? clamp(sceneFloorZ, mn.z, mx.z) : center.z;

    // Front-to-back accumulation of lit color + transmittance through the box.
    float dt = (tF - tN) / float(steps);
    float trans = 1.0;
    vec3 col = vec3(0.0);
    for (int s = 0; s < MAX_STEPS; ++s) {
      if (s >= steps) break;
      float tt = tN + (float(s) + jitter) * dt;
      if (tt >= tF) break;
      vec3 wp = uCamPos + rayDir * tt;
      float hFade = exp(-uHeightFalloff * max((wp.z - floorZ) - fadeStart, 0.0));
      float n = mistFbm(wp * uNoiseScale + scroll);
      float mist = mix(1.0, n, uNoiseStrength);
      // Break the flat faces / straight edges into wisps: pull the surface
      // inward by up to `carve` where noise is low, so the boundary is irregular
      // (the interior stays solid). carve == 0 → the original hard box.
      float coverage = 1.0;
      if (carve > 1e-4) {
        vec3 dFace = min(wp - mn, mx - wp);
        float edgeDist = min(dFace.x, min(dFace.y, dFace.z));
        coverage = smoothstep(0.0, carve, edgeDist - carve * (1.0 - n));
      }
      // Horizontal fade-in: full inside the wall footprint, easing to zero over
      // uWidth beyond it, so the mist ramps up across a wide area instead of a
      // hard wall edge.
      vec2 dOut = max(abs(wp.xy - center.xy) - origHalf.xy, vec2(0.0));
      float widthFade =
          uWidth > 1e-4 ? 1.0 - smoothstep(0.0, uWidth, length(dOut)) : 1.0;
      float density = uDensity * hFade * max(mist, 0.0) * coverage * widthFade;
      if (density <= 0.0001) continue;

      // Self-shadowing is what makes the mist read as a 3D VOLUME: a few cheap
      // density taps toward the sun estimate how much fog is between this point
      // and the light, so the depths darken while lit edges stay bright.
      vec3 sampleCol = litFog;
      if (uLighting != 0) {
        float sd = 0.0;
        for (int j = 1; j <= 3; ++j) {
          vec3 lp = wp + sunDir * (lightStep * float(j));
          sd += clamp(fbm(lp * uNoiseScale + scroll), 0.0, 1.0);
        }
        float lightT = exp(-sd * uDensity * lightStep * 0.2);
        sampleCol = uFogColor * (uFogAmbient + (1.0 - uFogAmbient) * lightT) +
                    uScatterColor * (uScatterStrength * phase * lightT);
      }

      float a = 1.0 - exp(-density * dt);
      col += trans * a * sampleCol;
      trans *= 1.0 - a;
      if (trans < 0.02) break;  // effectively opaque
    }

    float w = exp(-(tN - minTN) / kBlend);
    cAccum += w * col;
    aAccum += w * (1.0 - trans);
    wsum += w;
  }

  vec3 fogColor = cAccum / max(wsum, 1e-5);
  float alpha = aAccum / max(wsum, 1e-5);
  FragColor = vec4(fogColor, 1.0 - alpha);
}
