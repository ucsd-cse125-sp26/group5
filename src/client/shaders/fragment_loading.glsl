#version 410 core
in vec3 vNormal;
out vec4 FragColor;

void main() {
  // Encode the cube's face normal as RGB so each face reads as a different
  // pastel — gives a clear sense of rotation without needing a real light.
  vec3 n = normalize(vNormal);
  vec3 c = n * 0.5 + 0.5;
  FragColor = vec4(c, 1.0);
}
