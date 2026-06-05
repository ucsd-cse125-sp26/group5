#version 410 core

// Shared by both video modes. Converts planar YUV420 (3x GL_R8 planes) to RGB
// via BT.601. The fullscreen overlay draws to the backbuffer after tonemap, so
// it outputs display-referred RGB as-is (linearize=0). The in-world quad writes
// into the linear HDR buffer that tonemap later gamma-encodes, so it must
// output linear RGB (linearize=1) to avoid double gamma.

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D texY;
uniform sampler2D texCb;
uniform sampler2D texCr;
uniform vec2 texScale;      // visible fraction of the macroblock-padded planes
uniform vec2 fit;           // letterbox scale of the drawn surface (1,1 = none)
uniform int linearize;      // 1 = sRGB->linear (in-world HDR path)
uniform float emissiveBoost;

const mat4 rec601 = mat4(
    1.16438,  0.00000,  1.59603, -0.87079,
    1.16438, -0.39176, -0.81297,  0.52959,
    1.16438,  2.01723,  0.00000, -1.08139,
    0.0,      0.0,      0.0,       1.0);

void main() {
  // Map the drawn-surface UV into the centered video rect (letterbox).
  vec2 c = (vUV - 0.5) * fit + 0.5;
  if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0) {
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  // pl_mpeg rows are top-down; GL samples bottom-up, so flip V. Then crop to the
  // visible (non-padded) region of each plane.
  vec2 uv = vec2(c.x, 1.0 - c.y) * texScale;
  float y = texture(texY, uv).r;
  float cb = texture(texCb, uv).r;
  float cr = texture(texCr, uv).r;
  vec3 rgb = clamp((vec4(y, cb, cr, 1.0) * rec601).rgb, 0.0, 1.0);
  if (linearize == 1) rgb = pow(rgb, vec3(2.2));
  FragColor = vec4(rgb * emissiveBoost, 1.0);
}
