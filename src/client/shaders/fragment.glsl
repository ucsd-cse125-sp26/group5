#version 410 core
in vec3 normalFromVert;
in vec2 texCoordsFromVert;
in vec3 fragPos;
in vec4 fragPosLightSpace;

out vec4 FragColor;

uniform vec3 viewPos;
uniform sampler2D dirShadowMap;
uniform samplerCubeArray pointShadowMaps;
uniform float pointFarPlane;

// Roughly orthogonal directions for cubemap PCF. From learnopengl.
const vec3 sampleOffsetDirections[20] = vec3[](
  vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1),
  vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
  vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
  vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
  vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

float PointShadowFactor(int lightIdx, vec3 fragWorldPos, vec3 lightPos) {
  vec3 frag2light = fragWorldPos - lightPos;
  float currentDepth = length(frag2light);
  if (currentDepth >= pointFarPlane) return 0.0;

  float viewDistance = length(viewPos - fragWorldPos);
  float diskRadius = (1.0 + viewDistance / pointFarPlane) / 25.0;
  float bias = 0.05;

  float shadow = 0.0;
  for (int i = 0; i < 20; ++i) {
    vec3 offset = sampleOffsetDirections[i] * diskRadius;
    float closest = texture(pointShadowMaps,
                            vec4(frag2light + offset, lightIdx)).r;
    closest *= pointFarPlane;
    if (currentDepth - bias > closest) shadow += 1.0;
  }
  return shadow / 20.0;
}

struct Material {
  sampler2D ambient;
  sampler2D diffuse;
  sampler2D specular;
  sampler2D emissive;
  float shininess;
};
uniform Material material;

struct DirLight {
  vec3 direction;

  vec3 ambient;
  vec3 diffuse;
  vec3 specular;
};
uniform DirLight dirLight;

// Returns 1.0 if the fragment is in shadow, 0.0 if lit, with PCF 3x3 to
// soften the edge of the cascade. Slope-scaled bias avoids acne on flat
// surfaces nearly perpendicular to the sun.
float DirShadowFactor(vec3 normal, vec3 lightDir)
{
  // Perspective divide is a no-op for ortho but keeps this correct if the
  // matrix is ever made perspective.
  vec3 proj = fragPosLightSpace.xyz / fragPosLightSpace.w;
  proj = proj * 0.5 + 0.5;
  // Outside the light frustum's far plane: never shadow.
  if (proj.z > 1.0) return 0.0;
  // Outside x/y bounds: borderColor=(1,1,1,1) + CLAMP_TO_BORDER returns 1.0,
  // so the shadow test naturally fails there. No explicit guard needed.

  // Slope-scaled bias is applied via glPolygonOffset on the depth pass —
  // a tiny constant guard here only handles depth quantization.
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

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
{
  vec3 lightDir = normalize(-light.direction);
  // diffuse shading
  float diff = max(dot(normal, lightDir), 0.0);
  // Blinn-Phong specular
  vec3 halfwayDir = normalize(lightDir + viewDir);
  float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
  // combine results
  vec3 ambient = light.ambient * vec3(texture(material.diffuse, texCoordsFromVert));
  vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, texCoordsFromVert));
  vec3 specular = light.specular * spec * vec3(texture(material.specular, texCoordsFromVert));
  // Ambient stays unshadowed (light scatters); only block direct light.
  float shadow = DirShadowFactor(normal, lightDir);
  return ambient + (1.0 - shadow) * (diffuse + specular);
}

struct PointLight {
  vec3 position;

  float constant;
  float linear;
  float quadratic;

  vec3 ambient;
  vec3 diffuse;
  vec3 specular;
};
#define NR_POINT_LIGHTS 4
uniform PointLight pointLights[NR_POINT_LIGHTS];

vec3 CalcPointLight(PointLight light, int lightIdx, vec3 normal,
                    vec3 fragPos, vec3 viewDir)
{
  vec3 lightDir = normalize(light.position - fragPos);
  // diffuse shading
  float diff = max(dot(normal, lightDir), 0.0);
  // Blinn-Phong specular
  vec3 halfwayDir = normalize(lightDir + viewDir);
  float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
  // attenuation
  float distance = length(light.position - fragPos);
  float attenuation = 1.0 / (light.constant + light.linear * distance +
        light.quadratic * (distance * distance));
  // combine results
  vec3 ambient = light.ambient * vec3(texture(material.diffuse, texCoordsFromVert));
  vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, texCoordsFromVert));
  vec3 specular = light.specular * spec * vec3(texture(material.specular, texCoordsFromVert));
  ambient *= attenuation;
  diffuse *= attenuation;
  specular *= attenuation;
  float shadow = PointShadowFactor(lightIdx, fragPos, light.position);
  return ambient + (1.0 - shadow) * (diffuse + specular);
}

void main()
{
  // properties
  vec3 norm = normalize(normalFromVert);
  vec3 viewDir = normalize(viewPos - fragPos);

  // phase 1: Directional lighting
  vec3 result = CalcDirLight(dirLight, norm, viewDir);
  // phase 2: Point lights
  for (int i = 0; i < NR_POINT_LIGHTS; i++)
    result += CalcPointLight(pointLights[i], i, norm, fragPos, viewDir);
  // phase 3: emissive
  result += vec3(texture(material.emissive, texCoordsFromVert));

  FragColor = vec4(result, 1.0);
}
