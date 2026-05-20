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
uniform sampler2DShadow dirShadowMap;
uniform samplerCubeArrayShadow pointShadowMaps;
uniform sampler2D celRamp;

uniform int celBands;
uniform float celBandEpsilon;
uniform int halfLambert;
uniform float celSpecularThreshold;
uniform float celSpecularEpsilon;
uniform int useRampTexture;

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

float DirShadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
  vec4 fragPosLightSpace = camera.lightSpaceMatrix * vec4(worldPos, 1.0);
  vec3 proj = fragPosLightSpace.xyz / fragPosLightSpace.w;
  proj = proj * 0.5 + 0.5;
  if (proj.z > 1.0) return 0.0;
  float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
  float refDepth = proj.z - bias;
  vec2 texelSize = 1.0 / vec2(textureSize(dirShadowMap, 0));
  float visibility = 0.0;
  for (int x = -1; x <= 1; ++x) {
    for (int y = -1; y <= 1; ++y) {
      visibility += texture(
          dirShadowMap, vec3(proj.xy + vec2(x, y) * texelSize, refDepth));
    }
  }
  return 1.0 - visibility / 9.0;
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
    vec3 ambient = dirLight.ambient * albedo * ssaoFactor;
    vec3 diffuse = dirLight.diffuse * diffFactor * albedo;
    vec3 specular = dirLight.specular * specFactor * specularTint;
    float shadow = DirShadowFactor(worldPos, norm, lightDir);
    result += ambient + (1.0 - shadow) * (diffuse + specular);
  }

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
    result += ambient + (1.0 - shadow) * (diffuse + specular);
  }

  result += emissive;
  FragColor = vec4(result, 1.0);

  float brightness = dot(result, vec3(0.2126, 0.7152, 0.0722));
  float knee = max(bloomThreshold * 0.1, 0.05);
  float weight = smoothstep(bloomThreshold - knee, bloomThreshold + knee,
                             brightness);
  BrightColor = vec4(result * weight, 1.0);
}
