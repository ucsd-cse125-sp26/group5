#version 410 core
in vec2 vUV;
layout(location = 0) out vec4 FragColor;    // litColor (HDR)
layout(location = 1) out vec4 BrightColor;  // bloom seed

uniform float bloomThreshold;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gSpecular;
uniform sampler2D gEmissive;
uniform sampler2D ssao;
// Hardware-PCF shadow samplers (COMPARE_REF_TO_TEXTURE + LEQUAL).
// texture() returns visibility in [0,1] — 1.0 = lit, 0.0 = in shadow.
uniform sampler2DShadow dirShadowMap;
uniform samplerCubeArrayShadow pointShadowMaps;

layout(std140) uniform CameraBlock {
  mat4 view;
  mat4 projection;
  mat4 lightSpaceMatrix;
  vec3 viewPos;
  float pointFarPlane;
} camera;

struct DirLight {
  vec3 direction;
  vec3 ambient;
  vec3 diffuse;
  vec3 specular;
};
uniform DirLight dirLight;

// Cross-mode outlines (folded into the lighting pass rather than a separate
// post-process). 0 = off. Thresholds are shared with the Sobel mode so the
// outline tab in the settings UI tunes both.
uniform int outlineCross;
uniform vec3 outlineColor;
uniform float outlineDepthThreshold;
uniform float outlineNormalThreshold;

struct PointLight {
  vec3 position;
  float constant;
  float linear;
  float quadratic;
  vec3 ambient;
  vec3 diffuse;
  vec3 specular;
  int shadowIdx;  // -1 = non-shadow-casting, else cubemap-array layer
};
uniform PointLight pointLights[K_MAX_LIGHTING_SHADER_LIGHTS];
uniform int numPointLights;

const vec3 sampleOffsetDirections[20] = vec3[](
  vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1),
  vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
  vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
  vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
  vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

// Directional PCF kernel half-width (1 = 3x3) and tap-spacing multiplier;
// defaults (1, 1.0) reproduce the original hard-ish 9-tap shadow.
uniform int dirPcfRadius;
uniform float dirShadowSoftness;

float DirShadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
  vec4 fragPosLightSpace = camera.lightSpaceMatrix * vec4(worldPos, 1.0);
  vec3 proj = fragPosLightSpace.xyz / fragPosLightSpace.w;
  proj = proj * 0.5 + 0.5;
  if (proj.z > 1.0) return 0.0;
  // Slope-scaled bias — grazing surfaces need more.
  float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
  float refDepth = proj.z - bias;
  // (2R+1)² grid × 2×2 hardware PCF. dirPcfRadius 1 + dirShadowSoftness 1.0
  // reproduces the original 3×3 / 9-tap result exactly.
  vec2 texelSize = dirShadowSoftness / vec2(textureSize(dirShadowMap, 0));
  float visibility = 0.0;
  float taps = 0.0;
  for (int x = -dirPcfRadius; x <= dirPcfRadius; ++x) {
    for (int y = -dirPcfRadius; y <= dirPcfRadius; ++y) {
      visibility += texture(
          dirShadowMap, vec3(proj.xy + vec2(x, y) * texelSize, refDepth));
      taps += 1.0;
    }
  }
  return 1.0 - visibility / max(taps, 1.0);
}

float PointShadowFactor(int shadowIdx, vec3 worldPos, vec3 lightPos,
                         vec3 normal) {
  if (shadowIdx < 0) return 0.0;
  vec3 frag2light = worldPos - lightPos;
  float currentDepth = length(frag2light);
  if (currentDepth >= camera.pointFarPlane) return 0.0;
  // Back-facing fragments are self-shadowed; skip the 20-tap PCF.
  vec3 lightDir = -frag2light / max(currentDepth, 1e-4);
  if (dot(normal, lightDir) <= 0.0) return 1.0;
  float viewDistance = length(camera.viewPos - worldPos);
  float diskRadius = (1.0 + viewDistance / camera.pointFarPlane) / 25.0;
  float bias = 0.05;
  float refDepth = (currentDepth - bias) / camera.pointFarPlane;
  float visibility = 0.0;
  for (int i = 0; i < 20; ++i) {
    vec3 offset = sampleOffsetDirections[i] * diskRadius;
    visibility += texture(pointShadowMaps,
                           vec4(frag2light + offset, shadowIdx), refDepth);
  }
  return 1.0 - visibility / 20.0;
}

// Cross-shaped 4-tap probe on gNormal + gPosition. Returns 1 along
// silhouettes (sky-adjacent), creases (normal jump > normalThreshold), and
// depth discontinuities (Δd > depthThreshold). 0 on smooth surfaces.
float OutlineStrength(vec3 worldPos, vec3 norm) {
  vec2 texel = 1.0 / vec2(textureSize(gNormal, 0));
  vec2 offsets[4] = vec2[](
    vec2( texel.x, 0.0), vec2(-texel.x, 0.0),
    vec2(0.0,  texel.y), vec2(0.0, -texel.y)
  );
  float refDepth = length(camera.viewPos - worldPos);
  float normalDiff = 0.0;
  float depthDiff = 0.0;
  float skyHit = 0.0;
  for (int i = 0; i < 4; ++i) {
    vec2 uv = vUV + offsets[i];
    vec4 pN = texture(gPosition, uv);
    if (pN.a == 0.0) { skyHit = 1.0; continue; }
    vec3 nN = normalize(texture(gNormal, uv).rgb);
    normalDiff = max(normalDiff, 1.0 - max(dot(norm, nN), 0.0));
    depthDiff = max(depthDiff, abs(refDepth - length(camera.viewPos - pN.rgb)));
  }
  // Match Sobel's relative depth semantics: edge when Δd > threshold * d.
  float normEdge = step(outlineNormalThreshold, normalDiff);
  float depthEdge = step(outlineDepthThreshold * refDepth, depthDiff);
  return clamp(max(max(normEdge, depthEdge), skyHit), 0.0, 1.0);
}

void main() {
  vec4 posSample = texture(gPosition, vUV);
  // a==0 = sky/unwritten; let the skybox pass own this fragment.
  if (posSample.a == 0.0) discard;
  vec3 worldPos = posSample.rgb;
  vec4 normSample = texture(gNormal, vUV);
  vec3 norm = normalize(normSample.rgb);
  float shininess = normSample.a;
  vec3 albedo = texture(gAlbedo, vUV).rgb;
  vec3 specularTint = texture(gSpecular, vUV).rgb;
  vec3 emissive = texture(gEmissive, vUV).rgb;
  float ssaoFactor = texture(ssao, vUV).r;  // applied to ambient only
  vec3 viewDir = normalize(camera.viewPos - worldPos);

  vec3 result = vec3(0.0);
  {
    vec3 lightDir = normalize(-dirLight.direction);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 halfway = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfway), 0.0), shininess);
    vec3 ambient = dirLight.ambient * albedo * ssaoFactor;
    vec3 diffuse = dirLight.diffuse * diff * albedo;
    vec3 specular = dirLight.specular * spec * specularTint;
    float shadow = DirShadowFactor(worldPos, norm, lightDir);
    result += ambient + (1.0 - shadow) * (diffuse + specular);
  }

  vec3 pointResult = vec3(0.0);  // colored point-light contribution only
  for (int i = 0; i < numPointLights; ++i) {
    PointLight L = pointLights[i];
    vec3 toLight = L.position - worldPos;
    float dist = length(toLight);
    float attenuation = 1.0 /
        (L.constant + L.linear * dist + L.quadratic * dist * dist);
    // ~1/255 ≈ one LDR step; bail before the expensive PCF + lighting.
    if (attenuation < 0.004) continue;
    vec3 lightDir = toLight / max(dist, 1e-4);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 halfway = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfway), 0.0), shininess);
    vec3 ambient = L.ambient * albedo * ssaoFactor;
    vec3 diffuse = L.diffuse * diff * albedo;
    vec3 specular = L.specular * spec * specularTint;
    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;
    float shadow = PointShadowFactor(L.shadowIdx, worldPos, L.position, norm);
    vec3 contribution = ambient + (1.0 - shadow) * (diffuse + specular);
    result += contribution;
    pointResult += contribution;
  }

  result += emissive;

  if (outlineCross != 0) {
    result = mix(result, outlineColor, OutlineStrength(worldPos, norm));
  }

  // Fraction of this pixel's brightness from the (colored) point lights. The
  // tonemap reads it from litColor.a to keep point-lit areas in color even
  // where the B&W color-restoration transform would otherwise desaturate them.
  // Value (max channel) of the colored point-light contribution — absolute and
  // hue-independent, so color is kept only where the light actually reaches. A
  // luminance *ratio* would blow up in dark areas far from the light and tint
  // the wrong parts of the map.
  float pointVal = max(pointResult.r, max(pointResult.g, pointResult.b));
  float coloredLightKeep = smoothstep(0.04, 0.30, pointVal);
  FragColor = vec4(result, coloredLightKeep);

  // Soft-knee bright-pass for bloom.
  float brightness = dot(result, vec3(0.2126, 0.7152, 0.0722));
  float knee = max(bloomThreshold * 0.1, 0.05);
  float weight = smoothstep(bloomThreshold - knee, bloomThreshold + knee,
                             brightness);
  BrightColor = vec4(result * weight, 1.0);
}
