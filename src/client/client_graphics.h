#pragma once

#include <entt/entt.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "asset.h"
#include "client/animation.h"
#include "client/client_game.h"
#include "client/graphics_settings.h"
#include "client/shaders.h"
#include "glad/gl.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"

struct GLFWwindow;
struct GLFWmonitor;
class ClientNetwork;

struct CameraState {
  glm::vec3 position;
  glm::mat4 view;
};

std::optional<CameraState> computeCamera(const ClientGame& game);

enum class DebugChannel {
  Off,
  DirShadowMap,
  GPosition,
  GNormal,
  GAlbedo,
  GSpecular,
  GEmissive,
  Ssao,
  SsaoBlur,
  LitColor,
  BrightColor,
  LdrColor,
  Count,
};

struct Graphics {
  GLFWwindow* window = nullptr;
  std::optional<Shader> gbufferShader;
  std::optional<Shader> lightingShader;
  std::optional<Shader> lightingCelShader;
  std::optional<Shader> outlineSobelShader;
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

  // Post-process outline (Sobel) writes here when enabled.
  GLuint sobelFBO = 0;
  GLuint sobelColor = 0;

  // Optional 1D cel ramp texture loaded from settings.celRampPath.
  GLuint celRampTexture = 0;
  std::string lastCelRampPath = "";

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

  GraphicsSettings settings;

  // Tracked from settings to detect resolution changes and reallocate.
  int lastDirShadowSize = 0;
  int lastPointShadowSize = 0;
  bool prevShadowsEnabled = true;

  GLuint dirShadowFBO = 0;
  GLuint dirShadowMap = 0;
  glm::mat4 lightSpaceMatrix{1.0f};
  std::optional<Shader> shadowDirShader;

  // 4 cubemaps × 6 faces = 24 layers populated via multi-pass per-face
  // rendering.
  GLuint pointShadowFBO = 0;
  GLuint pointShadowMaps = 0;
  std::optional<Shader> shadowPointShader;

  int fbWidth = 0;
  int fbHeight = 0;
  // Offscreen render resolution = fbWidth/Height / pixelationScale; the
  // present pass upscales this with GL_NEAREST to the default framebuffer.
  int renderWidth = 0;
  int renderHeight = 0;
  int lastPixelationScale = 1;

  // Tracks the active palette size so a change can trigger per-model
  // k-means rebuilds. The palette itself lives on each Model.
  int lastPaletteColors = 0;

  // Skeletal animation. AnimationLibrary is built lazily once per skinned
  // model; Animator state lives per entity and is garbage-collected when
  // the entity disappears from the registry.
  std::unordered_map<std::string, std::unique_ptr<AnimationLibrary>>
      animationLibraries;
  std::unordered_map<entt::entity, Animator> animators;
  // glfwGetTime() at the previous render() call; 0 on first frame.
  double lastFrameTime = 0.0;

  bool fullscreen = false;
  int windowedX = 0;
  int windowedY = 0;
  int windowedW = 0;
  int windowedH = 0;

  bool keyF2Prev = false;
  bool keyF5Prev = false;
  bool keyF11Prev = false;

  bool settingsMenuOpen = false;
  bool keySettingsMenuPrev = false;
  bool keyEscapePrev = false;
  bool prevSyncedMenuOpen = false;

  DebugChannel debugChannel = DebugChannel::Off;

  // Bound for fullscreen-triangle draws; positions come from gl_VertexID.
  GLuint fullscreenVAO = 0;

  // CameraBlock UBO at binding=0; mirrored by CameraUBOData in the .cpp.
  GLuint cameraUBO = 0;

  // Tiny self-contained loading scene: standalone cube VAO/VBO/EBO + a
  // minimal shader that only reads position + normal. None of the main
  // rendering machinery needs to be online for it to draw.
  std::optional<Shader> loadingShader;
  GLuint loadingCubeVAO = 0;
  GLuint loadingCubeVBO = 0;
  GLuint loadingCubeEBO = 0;
  int loadingCubeIndexCount = 0;
  double loadingStartTime = 0.0;

  bool load(int width, int height);
  void render(ClientGame& game, ClientNetwork& network);
  void swap();
  ~Graphics();

  void resizeBuffers(int width, int height);
  void reloadShaders();
  void initShaderUniforms();
  void toggleFullscreen();
  void cycleDebugChannel();
  void processDebugKeys();
  void drawDebugOverlay();

  void initImGui();
  void shutdownImGui();
  void drawSettingsUIFrame(ClientGame& game);

  // Loading screen: minimal self-contained renderer that runs before the
  // rest of the pipeline is online.
  void initLoadingScreen();
  void destroyLoadingScreen();
  void renderLoadingFrame(const std::string& status);

  void allocateDirShadowMap(int size);
  void allocatePointShadowMaps(int size);
  void clearShadowMaps();

  // Reloads celRampTexture if settings.celRampPath changed since last call.
  void ensureCelRampLoaded();
};
