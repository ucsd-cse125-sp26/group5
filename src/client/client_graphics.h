#pragma once

#include <optional>
#include <string>
#include <unordered_map>

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
  std::optional<Shader> shader;
  std::optional<Shader> skyboxShader;
  std::optional<Shader> presentShader;
  std::optional<Shader> debugOverlay;
  std::unordered_map<std::string, Model*> models;
  std::unordered_map<std::string, Skybox> skyboxes;
  glm::mat4 projection{1.0f};

  // Offscreen scene target. The forward + skybox pass writes here; the
  // present pass samples sceneColor and blits it to the default framebuffer.
  // Recreated by resizeBuffers when the framebuffer size changes. Phase 5
  // upgrades sceneColor to RGBA16F for HDR.
  GLuint sceneFBO = 0;
  GLuint sceneColor = 0;
  GLuint sceneDepth = 0;

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
