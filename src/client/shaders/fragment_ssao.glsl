#version 410 core
// Screen-space ambient occlusion. Reads world-space position + normal from
// the g-buffer, converts them to view-space (where the hemisphere kernel
// math is defined), samples 64 points around the fragment, and counts how
// many are behind real geometry — that fraction becomes the occlusion.
in vec2 vUV;
out float FragColor;

uniform sampler2D gPosition;  // RGBA16F, .a = sky sentinel
uniform sampler2D gNormal;    // RGBA16F, .a = shininess (ignored here)
uniform sampler2D texNoise;   // 4x4 RGB16F, GL_REPEAT

uniform mat4 view;
uniform mat4 projection;

// Hemisphere kernel + tile-noise scale.
uniform vec3 samples[64];
uniform int kernelSize;
uniform float radius;
uniform float bias;
uniform vec2 noiseScale;  // screenSize / 4

void main() {
  vec4 worldSample = texture(gPosition, vUV);
  // Sky / unwritten g-buffer pixel: no occlusion (full ambient passes through).
  if (worldSample.a == 0.0) {
    FragColor = 1.0;
    return;
  }
  vec3 fragPos = (view * vec4(worldSample.rgb, 1.0)).xyz;
  vec3 worldNormal = texture(gNormal, vUV).rgb;
  vec3 normal = normalize(mat3(view) * worldNormal);

  // Random per-pixel rotation vector tiled across the screen.
  vec3 randomVec = texture(texNoise, vUV * noiseScale).rgb;
  // Gram-Schmidt orthogonalization to build a TBN that tilts the kernel.
  vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
  vec3 bitangent = cross(normal, tangent);
  mat3 TBN = mat3(tangent, bitangent, normal);

  float occlusion = 0.0;
  for (int i = 0; i < kernelSize; ++i) {
    vec3 samplePos = TBN * samples[i];        // tangent → view
    samplePos = fragPos + samplePos * radius;

    // Project sample to clip space, perspective divide, [0,1] for sampling.
    vec4 offset = projection * vec4(samplePos, 1.0);
    offset.xyz /= offset.w;
    offset.xyz = offset.xyz * 0.5 + 0.5;

    // Look up the actual geometry's view-space z at that screen position.
    vec4 worldHit = texture(gPosition, offset.xy);
    float sampleDepth = (view * vec4(worldHit.rgb, 1.0)).z;

    // Range-check so distant geometry doesn't darken the fragment.
    float rangeCheck = smoothstep(0.0, 1.0,
                                   radius / abs(fragPos.z - sampleDepth));
    occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
  }
  occlusion = 1.0 - occlusion / float(kernelSize);
  // Power exposes a knob for darkening creases harder.
  FragColor = pow(occlusion, 2.0);
}
