#version 410 core

// Synthesizes a single screen-covering triangle from gl_VertexID with no
// vertex attributes. Bind an empty VAO and call glDrawArrays(GL_TRIANGLES, 0, 3).
// Vertex 0 = (-1,-1), 1 = (3,-1), 2 = (-1,3) — the rasterizer clips the
// off-screen overhang for free, and UVs are derived from clip-space xy.
out vec2 vUV;

void main() {
  vec2 pos = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
  vUV = pos * 0.5 + 0.5;
  gl_Position = vec4(pos, 0.0, 1.0);
}
