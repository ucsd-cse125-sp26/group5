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

enum class DebugChannel {
  Off,
  DirShadowMap,
  Count,
};

struct Graphics {
  GLFWwindow* window = nullptr;
  std::optional<Shader> gbufferShader;
  std::optional<Shader> lightingShader;
  std::optional<Shader> skyboxShader;
  std::optional<Shader> presentShader;
  std::optional<Shader> debugOverlay;
  std::unordered_map<std::string, Model*> models;
  std::unordered_map<std::string, Skybox> skyboxes;
  glm::mat4 projection{1.0f};

  // G-buffer (reallocated on resize):
  //   gPosition  RGBA16F  .rgb world pos, .a sky sentinel
  //   gNormal    RGBA16F  .rgb world normal, .a shininess
  //   gAlbedo    RGBA8    .rgb albedo
  //   gSpecular  RGBA8    .rgb specular tint
  //   gEmissive  RGBA8    .rgb emissive
  GLuint gBufferFBO = 0;
  GLuint gPosition = 0;
  GLuint gNormal = 0;
  GLuint gAlbedo = 0;
  GLuint gSpecular = 0;
  GLuint gEmissive = 0;
  GLuint gBufferDepth = 0;

  GLuint litFBO = 0;
  GLuint litColor = 0;
  GLuint brightColor = 0;
  GLuint litDepth = 0;

  GLuint pingFBO[2] = {0, 0};
  GLuint pingColor[2] = {0, 0};

  GLuint ldrFBO = 0;
  GLuint ldrColor = 0;

  std::optional<Shader> blurShader;
  std::optional<Shader> tonemapShader;
  std::optional<Shader> ssaoShader;
  std::optional<Shader> ssaoBlurShader;

  GLuint ssaoFBO = 0;
  GLuint ssaoColor = 0;
  GLuint ssaoBlurFBO = 0;
  GLuint ssaoBlurColor = 0;
  GLuint ssaoNoiseTex = 0;
  std::vector<glm::vec3> ssaoKernel;

  int ssaoKernelSize = 64;
  float ssaoRadius = 0.5f;
  float ssaoBias = 0.025f;

  float exposure = 1.0f;
  float bloomThreshold = 1.0f;
  float bloomStrength = 1.0f;
  int bloomBlurIterations = 10;

  GLuint dirShadowFBO = 0;
  GLuint dirShadowMap = 0;
  glm::mat4 lightSpaceMatrix{1.0f};
  std::optional<Shader> shadowDirShader;

  // 4 cubemaps × 6 faces = 24 layers populated via multi-pass per-face rendering.
  GLuint pointShadowFBO = 0;
  GLuint pointShadowMaps = 0;
  std::optional<Shader> shadowPointShader;

  int fbWidth = 0;
  int fbHeight = 0;

  bool fullscreen = false;
  int windowedX = 0;
  int windowedY = 0;
  int windowedW = 0;
  int windowedH = 0;

  bool keyF2Prev = false;
  bool keyF5Prev = false;
  bool keyF11Prev = false;

  DebugChannel debugChannel = DebugChannel::Off;

  // Bound for fullscreen-triangle draws; positions come from gl_VertexID.
  GLuint fullscreenVAO = 0;

  // CameraBlock UBO at binding=0; mirrored by CameraUBOData in the .cpp.
  GLuint cameraUBO = 0;

  bool load(int width, int height);
  void render(ClientGame& game);
  void swap();
  ~Graphics();

  void resizeBuffers(int width, int height);
  void reloadShaders();
  void initShaderUniforms();
  void toggleFullscreen();
  void cycleDebugChannel();
  void processDebugKeys();
  void drawDebugOverlay();
};
