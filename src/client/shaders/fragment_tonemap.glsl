#version 410 core
// HDR + bloom, exposure tonemap, gamma to LDR (no GL_FRAMEBUFFER_SRGB).
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
  vec3 mapped = vec3(1.0) - exp(-hdr * exposure);
  mapped = pow(mapped, vec3(1.0 / 2.2));
  FragColor = vec4(mapped, 1.0);
}
