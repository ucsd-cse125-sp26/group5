#version 410 core
// 24 invocations (4 lights × 6 faces); inactive slots get a kill matrix
// from the C++ side so their layers stay at cleared depth=1.0.
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
