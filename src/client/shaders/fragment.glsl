#version 460 core
in vec3 normalFromVert;
in vec2 texCoordsFromVert;
in vec3 fragPos;

out vec4 FragColor;

uniform vec3 viewPos;

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

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
{
  vec3 lightDir = normalize(-light.direction);
  // diffuse shading
  float diff = max(dot(normal, lightDir), 0.0);
  // specular shading
  vec3 reflectDir = reflect(-lightDir, normal);
  float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
  // combine results
  vec3 ambient = light.ambient * vec3(texture(material.diffuse, texCoordsFromVert));
  vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, texCoordsFromVert));
  vec3 specular = light.specular * spec * vec3(texture(material.specular, texCoordsFromVert));
  return (ambient + diffuse + specular);
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

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir)
{
  vec3 lightDir = normalize(light.position - fragPos);
  // diffuse shading
  float diff = max(dot(normal, lightDir), 0.0);
  // specular shading
  vec3 reflectDir = reflect(-lightDir, normal);
  float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
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
  return (ambient + diffuse + specular);
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
    result += CalcPointLight(pointLights[i], norm, fragPos, viewDir);
  // phase 3: emissive
  result += vec3(texture(material.emissive, texCoordsFromVert));

  FragColor = vec4(result, 1.0);
}
