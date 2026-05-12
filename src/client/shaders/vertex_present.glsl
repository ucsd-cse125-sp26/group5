#version 410 core

// Fullscreen triangle synthesized from gl_VertexID — bind empty VAO, draw 3.
out vec2 vUV;

void main() {
  vec2 pos = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
  vUV = pos * 0.5 + 0.5;
  gl_Position = vec4(pos, 0.0, 1.0);
}
