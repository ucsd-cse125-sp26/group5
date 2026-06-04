#version 410 core
// Debug channel preview. mode selects how the source texture is shown:
//   0 = direct rgb, 1 = normal-vis ([-1,1] -> [0,1]),
//   2 = HDR exposure tonemap, 3 = single R channel as gray,
//   4 = single R channel of srcArray layer srcLayer as gray (CSM cascade view).
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;
// Texture-array source used only by mode 4 (the directional CSM depth array,
// GL_TEXTURE_2D_ARRAY). A texture array cannot be sampled through sampler2D.
uniform sampler2DArray srcArray;
uniform int srcLayer;
uniform int mode;

void main() {
  vec3 c;
  if (mode == 4) {
    c = vec3(texture(srcArray, vec3(vUV, float(srcLayer))).r);
  } else {
    vec4 s = texture(src, vUV);
    if (mode == 1) {
      c = s.rgb * 0.5 + 0.5;
    } else if (mode == 2) {
      c = vec3(1.0) - exp(-s.rgb);
      c = pow(c, vec3(1.0 / 2.2));
    } else if (mode == 3) {
      c = vec3(s.r);
    } else {
      c = s.rgb;
    }
  }
  FragColor = vec4(c, 1.0);
}
