#version 410 core
in vec2 vUV;
// MRT outputs:
//   FragColor (loc 0)   = HDR lit color, lands in litFBO/litColor
//   BrightColor (loc 1) = HDR color where brightness > threshold; seeds the
//                         bloom blur ping-pong
layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 BrightColor;

uniform float bloomThreshold;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedo;
uniform sampler2D gSpecular;
uniform sampler2D gEmissive;
uniform sampler2D ssao;
uniform sampler2D dirShadowMap;
uniform samplerCubeArray pointShadowMaps;

uniform vec3 viewPos;
uniform mat4 lightSpaceMatrix;
uniform float pointFarPlane;

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
  // -1 if this light is non-shadow-casting; otherwise the cubemap-array
  // layer index 0..3.
  int shadowIdx;
};
#define NR_POINT_LIGHTS 32
uniform PointLight pointLights[NR_POINT_LIGHTS];
uniform int numPointLights;

const vec3 sampleOffsetDirections[20] = vec3[](
  vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1),
  vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
  vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
  vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
  vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

float DirShadowFactor(vec3 worldPos, vec3 normal, vec3 lightDir) {
  vec4 fragPosLightSpace = lightSpaceMatrix * vec4(worldPos, 1.0);
  vec3 proj = fragPosLightSpace.xyz / fragPosLightSpace.w;
  proj = proj * 0.5 + 0.5;
  if (proj.z > 1.0) return 0.0;
  float bias = 0.0005;
  float currentDepth = proj.z;
  vec2 texelSize = 1.0 / vec2(textureSize(dirShadowMap, 0));
  float shadow = 0.0;
  for (int x = -1; x <= 1; ++x) {
    for (int y = -1; y <= 1; ++y) {
      float closest = texture(dirShadowMap,
                              proj.xy + vec2(x, y) * texelSize).r;
      shadow += currentDepth - bias > closest ? 1.0 : 0.0;
    }
  }
  return shadow / 9.0;
}

float PointShadowFactor(int shadowIdx, vec3 worldPos, vec3 lightPos) {
  if (shadowIdx < 0) return 0.0;
  vec3 frag2light = worldPos - lightPos;
  float currentDepth = length(frag2light);
  if (currentDepth >= pointFarPlane) return 0.0;
  float viewDistance = length(viewPos - worldPos);
  float diskRadius = (1.0 + viewDistance / pointFarPlane) / 25.0;
  float bias = 0.05;
  float shadow = 0.0;
  for (int i = 0; i < 20; ++i) {
    vec3 offset = sampleOffsetDirections[i] * diskRadius;
    float closest = texture(pointShadowMaps,
                            vec4(frag2light + offset, shadowIdx)).r;
    closest *= pointFarPlane;
    if (currentDepth - bias > closest) shadow += 1.0;
  }
  return shadow / 20.0;
}

void main() {
  vec4 posSample = texture(gPosition, vUV);
  // Sky / unwritten pixel — let the skybox forward pass own this fragment.
  if (posSample.a == 0.0) discard;
  vec3 worldPos = posSample.rgb;
  vec4 normSample = texture(gNormal, vUV);
  vec3 norm = normalize(normSample.rgb);
  float shininess = normSample.a;
  vec3 albedo = texture(gAlbedo, vUV).rgb;
  vec3 specularTint = texture(gSpecular, vUV).rgb;
  vec3 emissive = texture(gEmissive, vUV).rgb;
  // Screen-space ambient occlusion factor; only modulates ambient terms.
  float ssaoFactor = texture(ssao, vUV).r;
  vec3 viewDir = normalize(viewPos - worldPos);

  vec3 result = vec3(0.0);

  // Directional light + shadow.
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

  // Point lights.
  for (int i = 0; i < numPointLights; ++i) {
    PointLight L = pointLights[i];
    vec3 toLight = L.position - worldPos;
    float dist = length(toLight);
    vec3 lightDir = toLight / max(dist, 1e-4);
    float attenuation = 1.0 /
        (L.constant + L.linear * dist + L.quadratic * dist * dist);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 halfway = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfway), 0.0), shininess);
    vec3 ambient = L.ambient * albedo * ssaoFactor;
    vec3 diffuse = L.diffuse * diff * albedo;
    vec3 specular = L.specular * spec * specularTint;
    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;
    float shadow = PointShadowFactor(L.shadowIdx, worldPos, L.position);
    result += ambient + (1.0 - shadow) * (diffuse + specular);
  }

  result += emissive;
  FragColor = vec4(result, 1.0);

  // Bright-pass extraction for bloom. Only contributes when the perceived
  // luminance of this pixel exceeds the threshold (default 1.0 in HDR).
  float brightness = dot(result, vec3(0.2126, 0.7152, 0.0722));
  BrightColor = brightness > bloomThreshold ? vec4(result, 1.0)
                                             : vec4(0.0, 0.0, 0.0, 1.0);
}
