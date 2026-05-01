#version 410 core
// Combines the HDR scene with the blurred bright pass, applies exposure
// tone mapping, and writes LDR. FXAA runs on this LDR output.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D hdrColor;
uniform sampler2D bloomColor;
uniform float exposure;
uniform float bloomStrength;

void main() {
  vec3 hdr = texture(hdrColor, vUV).rgb;
  vec3 bloom = texture(bloomColor, vUV).rgb;
  hdr += bloom * bloomStrength;
  // Exposure tonemap. Phase 5 default exposure=1.0; tweak for day/night.
  vec3 mapped = vec3(1.0) - exp(-hdr * exposure);
  // sRGB encode: the LDR target is RGBA8 with no GL_FRAMEBUFFER_SRGB, so
  // apply gamma here. ~2.2 is the de-facto display gamma.
  mapped = pow(mapped, vec3(1.0 / 2.2));
  FragColor = vec4(mapped, 1.0);
}
