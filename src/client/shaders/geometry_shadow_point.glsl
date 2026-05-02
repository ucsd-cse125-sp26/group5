#version 410 core
// Single-pass cubemap-array shadow rendering. 24 GS invocations = 4 lights ×
// 6 faces. Each invocation re-emits the input triangle into one layer-face
// of the cubemap-array depth attachment, transformed by that face's
// light-space matrix. Matrices for inactive light slots are zeroed so
// emitted triangles get clipped (z > 1 after divide), leaving the layer's
// cleared depth=1.0 untouched.
layout(triangles, invocations = K_POINT_SHADOW_LAYERS) in;
layout(triangle_strip, max_vertices = 3) out;

uniform mat4 shadowMatrices[K_POINT_SHADOW_LAYERS];

out vec4 fragWorldPos;
flat out int lightIdx;

void main() {
  gl_Layer = gl_InvocationID;
  lightIdx = gl_InvocationID / 6;
  for (int i = 0; i < 3; ++i) {
    fragWorldPos = gl_in[i].gl_Position;
    gl_Position = shadowMatrices[gl_InvocationID] * fragWorldPos;
    EmitVertex();
  }
  EndPrimitive();
}
