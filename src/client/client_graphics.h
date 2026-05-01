#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "asset.h"
#include "client/client_game.h"
#include "client/shaders.h"
#include "glad/gl.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"

struct GLFWwindow;
struct GLFWmonitor;

struct CameraState {
  glm::vec3 position;
  glm::mat4 view;
};

std::optional<CameraState> computeCamera(const ClientGame& game);

// Debug overlay channel cycled by F2. Phase 0 has only Off; later phases
// extend this enum with the textures they introduce (G-buffer attachments,
// shadow maps, SSAO, bloom intermediates, etc.).
enum class DebugChannel {
  Off,
  DirShadowMap,
  // PointShadow channels need a samplerCubeArray overlay variant — punt
  // until we add one.
  Count,
};

struct Graphics {
  GLFWwindow* window = nullptr;
  // Geometry pass: writes the g-buffer.
  std::optional<Shader> gbufferShader;
  // Deferred lighting pass: reads g-buffer + shadow maps, writes lit color.
  std::optional<Shader> lightingShader;
  std::optional<Shader> skyboxShader;
  // Final present pass — currently FXAA. Phase 5 inserts a tonemap pass
  // before this.
  std::optional<Shader> presentShader;
  std::optional<Shader> debugOverlay;
  std::unordered_map<std::string, Model*> models;
  std::unordered_map<std::string, Skybox> skyboxes;
  glm::mat4 projection{1.0f};

  // G-buffer. Reallocated on resize.
  //   gPosition  RGBA16F   (.rgb world-space pos, .a sky sentinel)
  //   gNormal    RGBA16F   (.rgb world-space normal, .a shininess)
  //   gAlbedo    RGBA8     (.rgb diffuse/albedo, .a unused)
  //   gSpecular  RGBA8     (.rgb specular tint, .a unused — could be
  //                          roughness in a future PBR variant)
  //   gEmissive  RGBA8     (.rgb emissive, .a unused)
  GLuint gBufferFBO = 0;
  GLuint gPosition = 0;
  GLuint gNormal = 0;
  GLuint gAlbedo = 0;
  GLuint gSpecular = 0;
  GLuint gEmissive = 0;
  GLuint gBufferDepth = 0;

  // Lit FBO: deferred lighting pass writes via MRT (litColor + brightColor),
  // skybox forward pass adds to litColor only. Both color attachments are
  // RGBA16F for HDR.
  GLuint litFBO = 0;
  GLuint litColor = 0;
  GLuint brightColor = 0;
  GLuint litDepth = 0;  // RB; gBuffer depth gets blitted in for skybox

  // Bloom blur ping-pong (RGBA16F). Seeded from brightColor; final pass
  // result is read by the tonemap step.
  GLuint pingFBO[2] = {0, 0};
  GLuint pingColor[2] = {0, 0};

  // Tone-mapped LDR output (RGBA8). FXAA samples this on the way to the
  // default framebuffer.
  GLuint ldrFBO = 0;
  GLuint ldrColor = 0;

  std::optional<Shader> blurShader;
  std::optional<Shader> tonemapShader;
  std::optional<Shader> ssaoShader;
  std::optional<Shader> ssaoBlurShader;

  // SSAO. Single-channel GL_RED textures sized to the framebuffer.
  GLuint ssaoFBO = 0;
  GLuint ssaoColor = 0;
  GLuint ssaoBlurFBO = 0;
  GLuint ssaoBlurColor = 0;
  // 4×4 GL_REPEAT noise texture of tangent-space rotation vectors.
  GLuint ssaoNoiseTex = 0;
  // 64-sample hemisphere kernel (vec3 each), generated once at load.
  std::vector<glm::vec3> ssaoKernel;

  // SSAO tunables.
  int ssaoKernelSize = 64;
  float ssaoRadius = 0.5f;
  float ssaoBias = 0.025f;

  // Tunables (CPU-side defaults).
  float exposure = 1.0f;
  float bloomThreshold = 1.0f;
  float bloomStrength = 1.0f;
  int bloomBlurIterations = 10;  // 5 H/V pairs

  // Directional shadow map. Allocated once at SHADOW_MAP_SIZE × itself,
  // independent of window size — never recreated by resizeBuffers.
  GLuint dirShadowFBO = 0;
  GLuint dirShadowMap = 0;
  glm::mat4 lightSpaceMatrix{1.0f};
  std::optional<Shader> shadowDirShader;

  // Point-light cubemap array shadow map: 4 cubemaps × 6 faces = 24 layers.
  // Single FBO; the shadow pass uses a geometry shader with 24 invocations
  // to populate every layer-face in one draw call.
  GLuint pointShadowFBO = 0;
  GLuint pointShadowMaps = 0;
  std::optional<Shader> shadowPointShader;

  // Framebuffer dimensions in pixels (HiDPI-aware).
  int fbWidth = 0;
  int fbHeight = 0;

  // Fullscreen toggle state. When entering fullscreen we stash the windowed
  // geometry so F11-again can restore it.
  bool fullscreen = false;
  int windowedX = 0;
  int windowedY = 0;
  int windowedW = 0;
  int windowedH = 0;

  // Edge-detection state for debug keys (F2/F5/F11).
  bool keyF2Prev = false;
  bool keyF5Prev = false;
  bool keyF11Prev = false;

  DebugChannel debugChannel = DebugChannel::Off;

  // Empty VAO bound when issuing the fullscreen-triangle draw. Vertex
  // positions are synthesized in the vertex shader from gl_VertexID. Reused
  // by the debug overlay (and, from phase 2 onward, the present pass).
  GLuint fullscreenVAO = 0;

  bool load(int width, int height);
  void render(ClientGame& game);
  void swap();
  ~Graphics();

  // Phase 0 infrastructure.
  void resizeBuffers(int width, int height);
  void reloadShaders();
  void initShaderUniforms();
  void toggleFullscreen();
  void cycleDebugChannel();
  // Polls F2/F5/F11 once per frame; called from main loop.
  void processDebugKeys();
  void drawDebugOverlay();
};
