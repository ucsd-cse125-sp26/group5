#version 410 core
// SSAO: kernel math runs in view space; reads world-space g-buffer.
in vec2 vUV;
out float FragColor;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D texNoise;

layout(std140) uniform CameraBlock {
  mat4 view;
  mat4 projection;
  mat4 lightSpaceMatrix;
  vec3 viewPos;
  float pointFarPlane;
} camera;

uniform vec3 samples[64];
uniform int kernelSize;
uniform float radius;
uniform float bias;
uniform vec2 noiseScale;

void main() {
  vec4 worldSample = texture(gPosition, vUV);
  if (worldSample.a == 0.0) {
    FragColor = 1.0;
    return;
  }
  vec3 fragPos = (camera.view * vec4(worldSample.rgb, 1.0)).xyz;
  vec3 worldNormal = texture(gNormal, vUV).rgb;
  vec3 normal = normalize(mat3(camera.view) * worldNormal);

  vec3 randomVec = texture(texNoise, vUV * noiseScale).rgb;
  vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
  vec3 bitangent = cross(normal, tangent);
  mat3 TBN = mat3(tangent, bitangent, normal);

  float occlusion = 0.0;
  for (int i = 0; i < kernelSize; ++i) {
    vec3 samplePos = TBN * samples[i];
    samplePos = fragPos + samplePos * radius;

    vec4 offset = camera.projection * vec4(samplePos, 1.0);
    offset.xyz /= offset.w;
    offset.xyz = offset.xyz * 0.5 + 0.5;

    vec4 worldHit = texture(gPosition, offset.xy);
    // Sky pixels would read worldHit.rgb=(0,0,0) and spuriously occlude.
    if (worldHit.a == 0.0) continue;
    float sampleDepth = (camera.view * vec4(worldHit.rgb, 1.0)).z;

    float rangeCheck = smoothstep(0.0, 1.0,
                                   radius / abs(fragPos.z - sampleDepth));
    occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
  }
  occlusion = 1.0 - occlusion / float(kernelSize);
  FragColor = pow(occlusion, 2.0);
}
