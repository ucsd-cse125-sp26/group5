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
uniform sampler2DArrayShadow dirShadowMap;
uniform samplerCubeArrayShadow pointShadowMaps;
uniform sampler2D celRamp;

uniform int celBands;
uniform float celBandEpsilon;
uniform int halfLambert;
uniform float celSpecularThreshold;
uniform float celSpecularEpsilon;
uniform int useRampTexture;

// Stylized Fresnel rim light; celRimStrength 0 = off (default).
uniform float celRimStrength;
uniform vec3 celRimColor;
uniform float celRimPower;
uniform float celRimThreshold;

layout(std140) uniform CameraBlock {
  mat4 view;
  mat4 projection;
  mat4 lightSpaceMatrices[K_SHADOW_CASCADE_COUNT];
  vec4 cascadeSplits;  // per-cascade FAR view-space depth (positive)
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

// Image-based-ambient approximation: skybox average color, blended into the
// global ambient by iblAmbientStrength (0 = flat dirLight.ambient, unchanged).
uniform vec3 iblAmbientColor;
uniform float iblAmbientStrength;

// Cross-mode outlines, same as in fragment_lighting_deferred.glsl.
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
  int shadowIdx;
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
// Cascaded shadow maps: dirShadowMap is a depth texture ARRAY, one layer per
// cascade. See fragment_lighting_deferred.glsl for the full rationale.
uniform int cascadeDebug;
uniform float cascadeBlendBand;

// First cascade whose far split is in front of this fragment. viewDepth is the
// positive forward distance -(view*worldPos).z (glm::lookAt looks down -Z).
int selectCascade(float viewDepth) {
  for (int i = 0; i < K_SHADOW_CASCADE_COUNT; ++i) {
    if (viewDepth < camera.cascadeSplits[i]) return i;
  }
  return K_SHADOW_CASCADE_COUNT - 1;
}

float sampleCascade(int cascade, vec3 worldPos, vec3 normal, vec3 lightDir) {
  vec4 fragPosLightSpace =
      camera.lightSpaceMatrices[cascade] * vec4(worldPos, 1.0);
  vec3 proj = fragPosLightSpace.xyz / fragPosLightSpace.w;
  proj = proj * 0.5 + 0.5;
  if (proj.z > 1.0) return 0.0;  // beyond this cascade's far → treat as lit
  float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
  float refDepth = proj.z - bias;
  vec2 texelSize = dirShadowSoftness / vec2(textureSize(dirShadowMap, 0).xy);
  float visibility = 0.0;
  float taps = 0.0;
  for (int x = -dirPcfRadius; x <= dirPcfRadius; ++x) {
    for (int y = -dirPcfRadius; y <= dirPcfRadius; ++y) {
      visibility += texture(
          dirShadowMap,
          vec4(proj.xy + vec2(x, y) * texelSize, float(cascade), refDepth));
      taps += 1.0;
    }
  }
  return 1.0 - visibility / max(taps, 1.0);
}

float DirShadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
  float viewDepth = -(camera.view * vec4(worldPos, 1.0)).z;
  int cascade = selectCascade(viewDepth);
  float shadow = sampleCascade(cascade, worldPos, normal, lightDir);
  if (cascadeBlendBand > 0.0 && cascade < K_SHADOW_CASCADE_COUNT - 1) {
    float farSplit = camera.cascadeSplits[cascade];
    float nearSplit = cascade == 0 ? 0.0 : camera.cascadeSplits[cascade - 1];
    float band = cascadeBlendBand * (farSplit - nearSplit);
    float t = clamp((viewDepth - (farSplit - band)) / max(band, 1e-4), 0.0, 1.0);
    if (t > 0.0) {
      shadow = mix(shadow,
                   sampleCascade(cascade + 1, worldPos, normal, lightDir), t);
    }
  }
  return shadow;
}

vec3 cascadeTint(vec3 worldPos) {
  vec3 tints[4] = vec3[](vec3(1.0, 0.3, 0.3), vec3(0.3, 1.0, 0.3),
                         vec3(0.3, 0.3, 1.0), vec3(1.0, 1.0, 0.3));
  return tints[selectCascade(-(camera.view * vec4(worldPos, 1.0)).z)];
}

float PointShadowFactor(int shadowIdx, vec3 worldPos, vec3 lightPos,
                         vec3 normal) {
  if (shadowIdx < 0) return 0.0;
  vec3 frag2light = worldPos - lightPos;
  float currentDepth = length(frag2light);
  if (currentDepth >= camera.pointFarPlane) return 0.0;
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

// Lambert → quantized diffuse factor.
float celDiffuseFactor(vec3 N, vec3 L) {
  float nDotL = dot(N, L);
  nDotL = (halfLambert == 1) ? (nDotL * 0.5 + 0.5) : max(nDotL, 0.0);
  if (useRampTexture == 1) {
    return texture(celRamp, vec2(clamp(nDotL, 0.0, 1.0), 0.5)).r;
  }
  // Procedural quantization with smoothed band edges (Karapetyan eps).
  float bands = max(float(celBands), 1.0);
  float scaled = nDotL * bands;
  float idx = floor(scaled);
  float frac = scaled - idx;
  float eps = max(celBandEpsilon, 1e-4);
  float blend = smoothstep(1.0 - eps, 1.0, frac);
  return clamp((idx + blend) / bands, 0.0, 1.0);
}

// Hard cel specular with optional soft edge.
float celSpecFactor(vec3 N, vec3 H, float shininess) {
  float specRaw = pow(max(dot(N, H), 0.0), shininess);
  float eps = max(celSpecularEpsilon, 1e-4);
  return smoothstep(celSpecularThreshold,
                    celSpecularThreshold + eps, specRaw);
}

// Cross-shaped 4-tap probe on gNormal + gPosition. Returns 1 along
// silhouettes, creases, and depth jumps; 0 on smooth surfaces.
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
  if (posSample.a == 0.0) discard;
  vec3 worldPos = posSample.rgb;
  vec4 normSample = texture(gNormal, vUV);
  vec3 norm = normalize(normSample.rgb);
  float shininess = normSample.a;
  vec3 albedo = texture(gAlbedo, vUV).rgb;
  vec3 specularTint = texture(gSpecular, vUV).rgb;
  vec3 emissive = texture(gEmissive, vUV).rgb;
  float ssaoFactor = texture(ssao, vUV).r;
  vec3 viewDir = normalize(camera.viewPos - worldPos);

  vec3 result = vec3(0.0);
  {
    vec3 lightDir = normalize(-dirLight.direction);
    float diffFactor = celDiffuseFactor(norm, lightDir);
    vec3 halfway = normalize(lightDir + viewDir);
    float specFactor = celSpecFactor(norm, halfway, shininess);
    vec3 iblAmbient = iblAmbientColor * (0.5 + 0.5 * norm.z);
    vec3 ambientSrc = mix(dirLight.ambient, iblAmbient, iblAmbientStrength);
    vec3 ambient = ambientSrc * albedo * ssaoFactor;
    vec3 diffuse = dirLight.diffuse * diffFactor * albedo;
    vec3 specular = dirLight.specular * specFactor * specularTint;
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
    if (attenuation < 0.004) continue;
    vec3 lightDir = toLight / max(dist, 1e-4);
    float diffFactor = celDiffuseFactor(norm, lightDir);
    vec3 halfway = normalize(lightDir + viewDir);
    float specFactor = celSpecFactor(norm, halfway, shininess);
    vec3 ambient = L.ambient * albedo * ssaoFactor;
    vec3 diffuse = L.diffuse * diffFactor * albedo;
    vec3 specular = L.specular * specFactor * specularTint;
    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;
    float shadow = PointShadowFactor(L.shadowIdx, worldPos, L.position, norm);
    vec3 contribution = ambient + (1.0 - shadow) * (diffuse + specular);
    result += contribution;
    pointResult += contribution;
  }

  result += emissive;

  // Hard-edged Fresnel rim to match the cel look (cel path only).
  if (celRimStrength > 0.0) {
    float rim = pow(1.0 - max(dot(norm, viewDir), 0.0), celRimPower);
    rim = step(celRimThreshold, rim);
    result += celRimColor * rim * celRimStrength;
  }

  if (outlineCross != 0) {
    result = mix(result, outlineColor, OutlineStrength(worldPos, norm));
  }

  if (cascadeDebug != 0) {
    result = mix(result, cascadeTint(worldPos), 0.25);
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

  float brightness = dot(result, vec3(0.2126, 0.7152, 0.0722));
  float knee = max(bloomThreshold * 0.1, 0.05);
  float weight = smoothstep(bloomThreshold - knee, bloomThreshold + knee,
                             brightness);
  BrightColor = vec4(result * weight, 1.0);
}
