#version 410 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D src;             // tonemapped scene color (LDR)
uniform sampler2D gNormal;
uniform sampler2D gPosition;       // .rgb world pos, .a sky sentinel

layout(std140) uniform CameraBlock {
  mat4 view;
  mat4 projection;
  mat4 lightSpaceMatrix;
  vec3 viewPos;
  float pointFarPlane;
} camera;

uniform float outlineSobelWidth;    // pixel radius multiplier
uniform float outlineDepthThreshold;
uniform float outlineNormalThreshold;
uniform vec3  outlineColor;

float camDist(vec4 gPos) {
  // Skip sky-marked pixels — caller compares against zero to ignore them.
  return gPos.a > 0.0 ? length(gPos.rgb - camera.viewPos) : 0.0;
}

void main() {
  vec3 c = texture(src, vUV).rgb;
  vec2 t = (1.0 / vec2(textureSize(src, 0))) * outlineSobelWidth;

  vec3 nC = normalize(texture(gNormal, vUV).xyz);
  vec3 nL = normalize(texture(gNormal, vUV - vec2(t.x, 0.0)).xyz);
  vec3 nR = normalize(texture(gNormal, vUV + vec2(t.x, 0.0)).xyz);
  vec3 nU = normalize(texture(gNormal, vUV + vec2(0.0, t.y)).xyz);
  vec3 nD = normalize(texture(gNormal, vUV - vec2(0.0, t.y)).xyz);
  float normalEdge = 4.0 - dot(nC, nL) - dot(nC, nR)
                         - dot(nC, nU) - dot(nC, nD);

  vec4 pC = texture(gPosition, vUV);
  vec4 pL = texture(gPosition, vUV - vec2(t.x, 0.0));
  vec4 pR = texture(gPosition, vUV + vec2(t.x, 0.0));
  vec4 pU = texture(gPosition, vUV + vec2(0.0, t.y));
  vec4 pD = texture(gPosition, vUV - vec2(0.0, t.y));

  // Use camera-relative depth, not world-z. World-z is just the z component
  // of world position — it varies wildly across a single flat surface that
  // isn't axis-aligned, and gives false edges (and large-scale "black" on
  // distant flat surfaces). Camera distance is intrinsically per-camera-ray
  // and behaves consistently regardless of scene orientation.
  float dC = camDist(pC);
  float depthEdge = 0.0;
  if (dC > 0.0) {
    float dL = camDist(pL);
    float dR = camDist(pR);
    float dU = camDist(pU);
    float dD = camDist(pD);
    // Normalize by depth so distant surfaces don't trigger from perspective
    // foreshortening alone — small absolute deltas at depth would be
    // proportionally large relative to dC.
    if (dL > 0.0) depthEdge += abs(dC - dL) / dC;
    if (dR > 0.0) depthEdge += abs(dC - dR) / dC;
    if (dU > 0.0) depthEdge += abs(dC - dU) / dC;
    if (dD > 0.0) depthEdge += abs(dC - dD) / dC;
  }

  bool isEdge = pC.a > 0.0
              && (normalEdge > outlineNormalThreshold
                  || depthEdge > outlineDepthThreshold);
  FragColor = vec4(isEdge ? outlineColor : c, 1.0);
}
