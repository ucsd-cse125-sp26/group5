#version 410 core
// Composites the reduced-resolution barrier-fog march over the full-res scene.
// The march stored a premultiplied fog color in .rgb and the remaining
// transmittance in .a, so the blend is scene * a + rgb.
//
// Plain bilinear upsampling of a low-res volumetric bleeds fog across depth
// edges, haloing anything rendered in front of the fog. Instead we do a
// depth-aware (joint-bilateral) upsample: each of the four low-res fog taps is
// weighted by its bilinear weight AND by how closely the depth it was computed
// at matches this full-res pixel's depth. In smooth regions all four match and
// it reduces to bilinear; at a silhouette the wrong-depth taps are rejected, so
// the fog snaps to the correct side of the edge with no halo. At fogScale = 1
// (full res) the taps line up with the output and it passes through unchanged.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;        // full-res scene color (finalLDR)
uniform sampler2D fogTex;     // low-res fog: rgb = premult color, a = transmittance
uniform sampler2D gPosition;  // full-res .rgb world pos, .a == 0 => sky
uniform vec3 uCamPos;
uniform float uFarPlane;
uniform vec2 uFogTexel;  // 1 / fog-march resolution (one low-res texel in UV)

// Linear view distance used as the bilateral guide; sky maps to the far plane.
float linDepth(vec2 uv) {
  vec4 g = texture(gPosition, uv);
  return g.a > 0.0 ? length(g.rgb - uCamPos) : uFarPlane;
}

void main() {
  vec3 scene = texture(src, vUV).rgb;

  // The four low-res texel centers surrounding this pixel, and the bilinear
  // weights toward them.
  vec2 g = vUV / uFogTexel - 0.5;
  vec2 i0 = floor(g);
  vec2 fr = g - i0;
  vec2 uv00 = (i0 + 0.5) * uFogTexel;
  vec2 uv10 = uv00 + vec2(uFogTexel.x, 0.0);
  vec2 uv01 = uv00 + vec2(0.0, uFogTexel.y);
  vec2 uv11 = uv00 + uFogTexel;

  float dF = linDepth(vUV);
  float sigma = max(0.02 * dF, 1.0);  // depth tolerance in world units

  float w00 = (1.0 - fr.x) * (1.0 - fr.y) *
              exp(-abs(linDepth(uv00) - dF) / sigma);
  float w10 = fr.x * (1.0 - fr.y) * exp(-abs(linDepth(uv10) - dF) / sigma);
  float w01 = (1.0 - fr.x) * fr.y * exp(-abs(linDepth(uv01) - dF) / sigma);
  float w11 = fr.x * fr.y * exp(-abs(linDepth(uv11) - dF) / sigma);
  float wsum = w00 + w10 + w01 + w11;

  // If every tap's depth disagrees with this pixel (e.g. a thin feature the
  // low-res grid stepped over), all weights vanish. Fall back to plain bilinear
  // rather than dividing by ~0 — that division collapsed transmittance to 0 and
  // punched black specks. Bilinear always has a valid transmittance in .a.
  vec4 fog = wsum > 1e-3
                 ? (w00 * texture(fogTex, uv00) + w10 * texture(fogTex, uv10) +
                    w01 * texture(fogTex, uv01) + w11 * texture(fogTex, uv11)) /
                       wsum
                 : texture(fogTex, vUV);

  FragColor = vec4(scene * fog.a + fog.rgb, 1.0);
}
