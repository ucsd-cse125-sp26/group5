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
  Count,
};

struct Graphics {
  GLFWwindow* window = nullptr;
  std::optional<Shader> shader;
  std::optional<Shader> skyboxShader;
  std::optional<Shader> debugOverlay;
  std::unordered_map<std::string, Model*> models;
  std::unordered_map<std::string, Skybox> skyboxes;
  glm::mat4 projection{1.0f};

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
