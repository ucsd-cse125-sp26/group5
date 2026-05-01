// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
// clang-format on

#include "client_graphics.h"

#include <cmath>
#include <iostream>
#include <string>

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shared/assets.h"
#include "shared/components.h"
#include "shared/map_format.h"
#include "shared/simple_profiler.h"

// Skybox images use Y-up; the game uses Z-up.
// cubemap->game: X->X, Y->Z, Z->-Y  (column-major)
static const glm::mat3 kCubemapToGame(1, 0, 0, 0, 0, 1, 0, -1, 0);

// Resolution of the directional shadow map. 2048² gives crisp shadows at
// modest VRAM cost; bump if the scene grows larger.
static constexpr int kDirShadowMapSize = 2048;

// Point-light cubemap array. 4 lights × 6 faces × 1024² depth ≈ 24 MB.
static constexpr int kPointShadowSize = 1024;
static constexpr int kMaxPointLights = 4;
static constexpr int kPointShadowLayers = kMaxPointLights * 6;
// Linear-distance encoding range for point-light shadow depth.
static constexpr float kPointShadowNear = 0.1f;
static constexpr float kPointShadowFar = 50.0f;

static void initPointLights(const Shader& shader) {
  for (int pl = 0; pl < 4; pl++) {
    std::string prefix = "pointLights[" + std::to_string(pl) + "].";
    shader.setFloat(prefix + "constant", 1.0f);
    shader.setFloat(prefix + "linear", 0.0f);
    shader.setFloat(prefix + "quadratic", 0.0f);
    shader.setVec3(prefix + "ambient", 0.0f, 0.0f, 0.0f);
    shader.setVec3(prefix + "diffuse", 0.0f, 0.0f, 0.0f);
    shader.setVec3(prefix + "specular", 0.0f, 0.0f, 0.0f);
  }
}

static void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
  auto* g = static_cast<Graphics*>(glfwGetWindowUserPointer(w));
  if (g) g->resizeBuffers(width, height);
}

// Returns the active directional light direction (ECS override beats scene
// default). If no directional light is configured, returns a sensible
// fallback so the shadow pass still runs without crashing.
static glm::vec3 directionalLightDir(const ClientGame& game) {
  auto dlView = game.renderRegistry.view<shared::DirectionalLight>();
  for (auto ent : dlView) {
    const auto& dl = dlView.get<shared::DirectionalLight>(ent);
    return glm::vec3(dl.dirX, dl.dirY, dl.dirZ);
  }
  auto sceneView = game.renderRegistry.view<shared::Scene>();
  for (auto ent : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(ent);
    if (auto* info = shared::findScene(scene.name)) {
      return glm::vec3(info->dirX, info->dirY, info->dirZ);
    }
  }
  return glm::vec3(0.3f, 1.0f, -0.4f);
}

// Builds 24 light-space matrices for the point-light cubemap-array shadow
// pass. Active light slots get real per-face matrices; inactive slots get a
// "kill" matrix that produces clip-space z > 1, so emitted geometry is
// clipped and never writes to those layers (their cleared depth=1.0
// remains, meaning "not in shadow" when sampled). Also writes the per-light
// world-space positions into outPositions, padded with origins for
// inactive slots.
static void computePointShadowMatrices(const ClientGame& game,
                                       glm::mat4 outMatrices[kPointShadowLayers],
                                       glm::vec3 outPositions[kMaxPointLights]) {
  // Cube-face directions/ups. Same convention learnopengl uses; the cube
  // map is its own coordinate system, independent of the world's up axis.
  static const struct {
    glm::vec3 dir;
    glm::vec3 up;
  } faces[6] = {
      {{1, 0, 0}, {0, -1, 0}},   // +X
      {{-1, 0, 0}, {0, -1, 0}},  // -X
      {{0, 1, 0}, {0, 0, 1}},    // +Y
      {{0, -1, 0}, {0, 0, -1}},  // -Y
      {{0, 0, 1}, {0, -1, 0}},   // +Z
      {{0, 0, -1}, {0, -1, 0}},  // -Z
  };

  // Kill matrix: any input → clip space (0, 0, 2, 1) → NDC z = 2 → clipped.
  glm::mat4 kill(0.0f);
  kill[3] = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
  for (int i = 0; i < kPointShadowLayers; ++i) outMatrices[i] = kill;
  for (int i = 0; i < kMaxPointLights; ++i)
    outPositions[i] = glm::vec3(0.0f);

  glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f,
                                    kPointShadowNear, kPointShadowFar);

  int idx = 0;
  auto view = game.renderRegistry.view<shared::PointLight>();
  for (auto ent : view) {
    if (idx >= kMaxPointLights) break;
    const auto& pl = view.get<shared::PointLight>(ent);
    glm::vec3 p(pl.px, pl.py, pl.pz);
    outPositions[idx] = p;
    for (int f = 0; f < 6; ++f) {
      outMatrices[idx * 6 + f] =
          proj * glm::lookAt(p, p + faces[f].dir, faces[f].up);
    }
    ++idx;
  }
}

// Builds a light-space matrix that wraps an ortho frustum around the camera.
// The frustum follows the camera so distant objects get shadows too without
// burning the whole shadow map on the world origin.
static glm::mat4 computeDirectionalLightMatrix(const glm::vec3& cameraPos,
                                               const glm::vec3& lightDir) {
  glm::vec3 dir = glm::normalize(lightDir);
  // Place the virtual sun far enough behind the camera that the entire
  // frustum stays in front of it. 30 units works for the current scene scale.
  glm::vec3 lightPos = cameraPos - dir * 30.0f;
  glm::vec3 up = glm::abs(dir.z) > 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                        : glm::vec3(0.0f, 0.0f, 1.0f);
  glm::mat4 view = glm::lookAt(lightPos, cameraPos, up);
  // ±20 covers the visible play area; near=1, far=80 reaches across.
  glm::mat4 proj = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 80.0f);
  return proj * view;
}

std::optional<CameraState> computeCamera(const ClientGame& game) {
  auto selfIt = game.renderEntityMap.find(game.renderEntityId);
  if (selfIt == game.renderEntityMap.end() ||
      !game.renderRegistry.valid(selfIt->second) ||
      !game.renderRegistry.all_of<shared::Position, shared::Camera>(
          selfIt->second)) {
    return std::nullopt;
  }
  const auto& p = game.renderRegistry.get<shared::Position>(selfIt->second);
  const auto& cam = game.renderRegistry.get<shared::Camera>(selfIt->second);

  const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
  glm::quat playerRot(p.qw, p.qx, p.qy, p.qz);
  // Extract yaw only so entity pitch/roll doesn't tilt the camera.
  glm::vec3 flat = playerRot * glm::vec3(0.0f, 1.0f, 0.0f);
  flat.z = 0.0f;
  flat = glm::normalize(flat);
  float yaw = std::atan2(-flat.x, flat.y);
  glm::quat yawRot = glm::angleAxis(yaw, glm::vec3(0.0f, 0.0f, 1.0f));
  glm::quat pitchRot = glm::angleAxis(cam.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
  glm::vec3 forward = yawRot * pitchRot * glm::vec3(0.0f, 1.0f, 0.0f);

  glm::vec3 pos = glm::vec3(p.x, p.y, p.z + cam.ht);
  glm::mat4 view = glm::lookAt(pos, pos + forward, worldUp);
  return CameraState{.position = pos, .view = view};
}

static void setupCameraMatrix(const Shader& shader, const CameraState& camera) {
  shader.setMat4("view", camera.view);
  shader.setVec3("viewPos", camera.position.x, camera.position.y,
                 camera.position.z);
}

static const shared::SceneInfo* currentScene(const ClientGame& game) {
  auto sceneView = game.renderRegistry.view<shared::Scene>();
  for (auto ent : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(ent);
    auto* info = shared::findScene(scene.name);
    if (info) return info;
  }
  return nullptr;
}

static void updateDirectionalLight(const Shader& shader,
                                   const ClientGame& game) {
  // ECS DirectionalLight overrides the scene's default. First entity wins;
  // subsequent ones are ignored (shader has only one dirLight slot).
  auto dlView = game.renderRegistry.view<shared::DirectionalLight>();
  for (auto ent : dlView) {
    const auto& dl = dlView.get<shared::DirectionalLight>(ent);
    shader.setVec3("dirLight.direction", dl.dirX, dl.dirY, dl.dirZ);
    shader.setVec3("dirLight.ambient", dl.ambientR, dl.ambientG, dl.ambientB);
    shader.setVec3("dirLight.diffuse", dl.diffuseR, dl.diffuseG, dl.diffuseB);
    shader.setVec3("dirLight.specular", dl.specularR, dl.specularG,
                   dl.specularB);
    return;
  }
  auto* info = currentScene(game);
  if (!info) return;
  shader.setVec3("dirLight.direction", info->dirX, info->dirY, info->dirZ);
  shader.setVec3("dirLight.ambient", info->ambientR, info->ambientG,
                 info->ambientB);
  shader.setVec3("dirLight.diffuse", info->diffuseR, info->diffuseG,
                 info->diffuseB);
  shader.setVec3("dirLight.specular", info->specularR, info->specularG,
                 info->specularB);
}

static void updatePointLights(const Shader& shader, const ClientGame& game) {
  int plIdx = 0;
  auto lightView = game.renderRegistry.view<shared::PointLight>();
  for (auto ent : lightView) {
    if (plIdx >= 4) break;
    auto& pl = lightView.get<shared::PointLight>(ent);
    std::string prefix = "pointLights[" + std::to_string(plIdx) + "].";
    shader.setVec3(prefix + "position", pl.px, pl.py, pl.pz);
    shader.setFloat(prefix + "constant", pl.constant);
    shader.setFloat(prefix + "linear", pl.linear);
    shader.setFloat(prefix + "quadratic", pl.quadratic);
    shader.setVec3(prefix + "ambient", pl.ambientR, pl.ambientG, pl.ambientB);
    shader.setVec3(prefix + "diffuse", pl.diffuseR, pl.diffuseG, pl.diffuseB);
    shader.setVec3(prefix + "specular", pl.specularR, pl.specularG,
                   pl.specularB);
    plIdx++;
  }
}

static void drawSkybox(const Shader& shader, const Skybox& skybox,
                       const CameraState& camera, const glm::mat4& projection) {
  glm::mat4 skyboxView = glm::mat4(glm::mat3(camera.view) * kCubemapToGame);

  glDepthFunc(GL_LEQUAL);
  shader.use();
  shader.setMat4("view", skyboxView);
  shader.setMat4("projection", projection);

  glBindVertexArray(skybox.vao);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, skybox.cubemapTexture);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);
  glDepthFunc(GL_LESS);
}

static void renderEntities(const Shader& shader, ClientGame& game,
                           std::unordered_map<std::string, Model*>& models,
                           bool forShadowPass = false) {
  auto view = game.renderRegistry
                  .view<shared::Entity, shared::Position, shared::RenderInfo>();
  for (auto ent : view) {
    auto& p = view.get<shared::Position>(ent);
    auto& renderInfo = view.get<shared::RenderInfo>(ent);
    auto& entity = view.get<shared::Entity>(ent);
    if (forShadowPass) {
      // Light-marker meshes (the visible cube on a point/dir light) sit at
      // the light's origin; rendering them into the shadow map makes the
      // light shadow itself. Skip any entity whose render mesh represents
      // a light source.
      if (game.renderRegistry.any_of<shared::PointLight,
                                      shared::DirectionalLight>(ent))
        continue;
    } else {
      // Main pass: skip the camera entity (we'd see our own model
      // first-person otherwise).
      if (entity.id == game.renderEntityId) continue;
    }
    auto it = models.find(renderInfo.modelName);
    if (it == models.end() || !it->second) continue;
    Model* modelAsset = it->second;
    glm::quat rotation = glm::quat(p.qw, p.qx, p.qy, p.qz);
    auto model = glm::identity<glm::mat4>();
    model = glm::translate(model, glm::vec3(p.x, p.y, p.z));
    model = glm::scale(model,
                       glm::vec3(renderInfo.sx, renderInfo.sy, renderInfo.sz));
    model = model * glm::mat4_cast(rotation) *
            glm::mat4_cast(modelAsset->orientation);

    Draw(shader, *modelAsset, model);
  }
}

bool Graphics::load(int width, int height) {
  if (!glfwInit()) return false;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  window = glfwCreateWindow(width, height, "Hello World", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);  // vsync: cap to display refresh rate

  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported())
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

  // Stash this Graphics instance on the window so the framebuffer-size
  // callback can route resize events back into resizeBuffers.
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

  // Capture windowed geometry for fullscreen restore.
  glfwGetWindowPos(window, &windowedX, &windowedY);
  glfwGetWindowSize(window, &windowedW, &windowedH);
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

  int version = gladLoadGL(glfwGetProcAddress);
  printf("GL %d.%d\n", GLAD_VERSION_MAJOR(version),
         GLAD_VERSION_MINOR(version));

  shader.emplace("shaders/vertex.glsl", "shaders/fragment.glsl");
  skyboxShader.emplace("shaders/vertex_skybox.glsl",
                       "shaders/fragment_skybox.glsl");
  presentShader.emplace("shaders/vertex_present.glsl",
                        "shaders/fragment_present.glsl");
  shadowDirShader.emplace("shaders/vertex_shadow_dir.glsl",
                          "shaders/fragment_shadow_dir.glsl");
  shadowPointShader.emplace("shaders/vertex_shadow_point.glsl",
                            "shaders/fragment_shadow_point.glsl",
                            "shaders/geometry_shadow_point.glsl");
  debugOverlay.emplace("shaders/vertex_present.glsl",
                       "shaders/fragment_debug_overlay.glsl");

  // Allocate the directional shadow map once; size is independent of window
  // size so resizeBuffers leaves it alone.
  glGenFramebuffers(1, &dirShadowFBO);
  glGenTextures(1, &dirShadowMap);
  glBindTexture(GL_TEXTURE_2D, dirShadowMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kDirShadowMapSize,
               kDirShadowMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  // Outside the frustum returns depth=1.0 so DirShadowFactor reports unlit.
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, white);
  glBindFramebuffer(GL_FRAMEBUFFER, dirShadowFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         dirShadowMap, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "dirShadowFBO incomplete\n");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Point-light cubemap array shadow target. One depth attachment, 24
  // layer-faces (4 cubes × 6 faces). The whole array is bound to the FBO
  // via glFramebufferTexture so the GS can pick layers via gl_Layer.
  glGenFramebuffers(1, &pointShadowFBO);
  glGenTextures(1, &pointShadowMaps);
  glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, pointShadowMaps);
  glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT24,
               kPointShadowSize, kPointShadowSize, kPointShadowLayers, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S,
                  GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T,
                  GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R,
                  GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
  glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, pointShadowMaps,
                       0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "pointShadowFBO incomplete\n");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Empty VAO for fullscreen-triangle draws (positions synthesized in vert
  // shader via gl_VertexID).
  glGenVertexArrays(1, &fullscreenVAO);

  for (const auto& asset : shared::ASSETS) {
    Model* m = asset.cubeSpec ? makeCubeModel(*asset.cubeSpec)
                              : loadModel(std::string(asset.filename));
    if (!m) {
      fprintf(stderr, "Failed to load asset '%s' (%s)\n",
              std::string(asset.name).c_str(),
              std::string(asset.filename).c_str());
      continue;
    }
    m->orientation = glm::quat(asset.qw, asset.qx, asset.qy, asset.qz);
    models[std::string(asset.name)] = m;
    printf("Loaded asset: %s\n", std::string(asset.name).c_str());
  }

  // Register per-node sub-models from the active map. Server-side map_loader
  // spawns entities whose RenderInfo.modelName uses these same keys.
  auto mapModels = loadMapModels(shared::DEFAULT_MAP_PATH);
  for (auto& [key, m] : mapModels) {
    models[key] = m;
    printf("Loaded map sub-model: %s\n", key.c_str());
  }

  for (const auto& sc : shared::SCENES) {
    std::string dir = std::string(sc.skyboxDirectory);
    if (skyboxes.find(dir) == skyboxes.end()) {
      skyboxes[dir] = loadSkybox(dir);
      printf("Loaded skybox: %s (%s)\n", std::string(sc.name).c_str(),
             dir.c_str());
    }
  }

  glEnable(GL_DEPTH_TEST);

  resizeBuffers(fbWidth, fbHeight);
  initShaderUniforms();

  return true;
}

void Graphics::resizeBuffers(int width, int height) {
  if (width <= 0 || height <= 0) return;
  fbWidth = width;
  fbHeight = height;
  glViewport(0, 0, fbWidth, fbHeight);
  projection = glm::perspective(
      glm::radians(45.0f),
      static_cast<float>(fbWidth) / static_cast<float>(fbHeight), 0.1f, 100.0f);
  if (shader && shader->valid()) {
    shader->use();
    shader->setMat4("projection", projection);
  }

  // (Re)create the offscreen scene framebuffer at the new size. Phase 5
  // upgrades sceneColor's internal format to RGBA16F for HDR.
  if (!sceneFBO) glGenFramebuffers(1, &sceneFBO);
  if (!sceneColor) glGenTextures(1, &sceneColor);
  if (!sceneDepth) glGenRenderbuffers(1, &sceneDepth);

  glBindTexture(GL_TEXTURE_2D, sceneColor);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbWidth, fbHeight, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbWidth,
                        fbHeight);

  glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         sceneColor, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, sceneDepth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "sceneFBO incomplete\n");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Graphics::initShaderUniforms() {
  if (!shader || !shader->valid()) return;
  shader->use();
  shader->setMat4("projection", projection);
  initPointLights(*shader);
}

void Graphics::reloadShaders() {
  struct Reload {
    std::optional<Shader>& slot;
    const char* vert;
    const char* frag;
  };
  Reload reloads[] = {
      {shader, "shaders/vertex.glsl", "shaders/fragment.glsl"},
      {skyboxShader, "shaders/vertex_skybox.glsl",
       "shaders/fragment_skybox.glsl"},
      {presentShader, "shaders/vertex_present.glsl",
       "shaders/fragment_present.glsl"},
      {shadowDirShader, "shaders/vertex_shadow_dir.glsl",
       "shaders/fragment_shadow_dir.glsl"},
      {debugOverlay, "shaders/vertex_present.glsl",
       "shaders/fragment_debug_overlay.glsl"},
  };
  for (auto& r : reloads) {
    Shader candidate(r.vert, r.frag);
    if (candidate.valid()) {
      r.slot.emplace(std::move(candidate));
      printf("Reloaded: %s + %s\n", r.vert, r.frag);
    } else {
      fprintf(stderr, "Reload failed, keeping previous: %s + %s\n", r.vert,
              r.frag);
    }
  }
  // Geometry-stage reload (point shadow only).
  {
    Shader candidate("shaders/vertex_shadow_point.glsl",
                     "shaders/fragment_shadow_point.glsl",
                     "shaders/geometry_shadow_point.glsl");
    if (candidate.valid()) {
      shadowPointShader.emplace(std::move(candidate));
      printf("Reloaded: vertex_shadow_point + geometry_shadow_point + ...\n");
    } else {
      fprintf(stderr,
              "Reload failed, keeping previous: vertex_shadow_point + ...\n");
    }
  }
  initShaderUniforms();
}

void Graphics::toggleFullscreen() {
  if (!window) return;
  if (fullscreen) {
    glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedW,
                         windowedH, 0);
    fullscreen = false;
  } else {
    glfwGetWindowPos(window, &windowedX, &windowedY);
    glfwGetWindowSize(window, &windowedW, &windowedH);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return;
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                         mode->refreshRate);
    fullscreen = true;
  }
}

void Graphics::cycleDebugChannel() {
  int next = (static_cast<int>(debugChannel) + 1) %
             static_cast<int>(DebugChannel::Count);
  debugChannel = static_cast<DebugChannel>(next);
  const char* name = "?";
  switch (debugChannel) {
    case DebugChannel::Off:
      name = "Off";
      break;
    case DebugChannel::DirShadowMap:
      name = "DirShadowMap";
      break;
    case DebugChannel::Count:
      break;
  }
  printf("Debug overlay: %s\n", name);
}

void Graphics::processDebugKeys() {
  if (!window) return;
  bool f2 = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
  bool f5 = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
  bool f11 = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
  if (f2 && !keyF2Prev) cycleDebugChannel();
  if (f5 && !keyF5Prev) reloadShaders();
  if (f11 && !keyF11Prev) toggleFullscreen();
  keyF2Prev = f2;
  keyF5Prev = f5;
  keyF11Prev = f11;
}

void Graphics::drawDebugOverlay() {
  if (debugChannel == DebugChannel::Off) return;
  if (!debugOverlay || !debugOverlay->valid() || !fullscreenVAO) return;

  GLuint texToShow = 0;
  switch (debugChannel) {
    case DebugChannel::DirShadowMap:
      texToShow = dirShadowMap;
      break;
    case DebugChannel::Off:
    case DebugChannel::Count:
      return;
  }
  if (!texToShow) return;

  // Top-right quarter-screen tile.
  int cornerW = fbWidth / 4;
  int cornerH = fbHeight / 4;
  if (cornerW <= 0 || cornerH <= 0) return;
  int cornerX = fbWidth - cornerW;
  int cornerY = fbHeight - cornerH;

  glDisable(GL_DEPTH_TEST);
  glViewport(cornerX, cornerY, cornerW, cornerH);
  debugOverlay->use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texToShow);
  debugOverlay->setInt("src", 0);
  glBindVertexArray(fullscreenVAO);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glViewport(0, 0, fbWidth, fbHeight);
}

void Graphics::render(ClientGame& game) {
  SIMPLE_PROFILE_SCOPE("Render");
  auto camera = computeCamera(game);
  if (!camera) return;

  // Shadow pass: render scene depth from the directional light's POV into
  // dirShadowMap. The main scene pass below samples the result.
  lightSpaceMatrix = computeDirectionalLightMatrix(
      camera->position, directionalLightDir(game));
  if (shadowDirShader && shadowDirShader->valid()) {
    SIMPLE_PROFILE_SCOPE("ShadowDir");
    glBindFramebuffer(GL_FRAMEBUFFER, dirShadowFBO);
    glViewport(0, 0, kDirShadowMapSize, kDirShadowMapSize);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    // Hardware slope-scaled depth bias. Avoids the peter-panning we'd get
    // from front-face culling on the floor / non-watertight geometry while
    // still keeping shadow acne off flat surfaces. Tune the slope factor up
    // if acne returns; tune down if shadows detach from their casters.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    shadowDirShader->use();
    shadowDirShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    renderEntities(*shadowDirShader, game, models, /*forShadowPass=*/true);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // Point-light shadow pass: 24 cubemap-array layers populated in a single
  // draw call via geometry-shader instancing.
  glm::mat4 pointMats[kPointShadowLayers];
  glm::vec3 pointPositions[kMaxPointLights];
  computePointShadowMatrices(game, pointMats, pointPositions);
  if (shadowPointShader && shadowPointShader->valid()) {
    SIMPLE_PROFILE_SCOPE("ShadowPoint");
    glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
    glViewport(0, 0, kPointShadowSize, kPointShadowSize);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    shadowPointShader->use();
    shadowPointShader->setMat4Array(
        "shadowMatrices", kPointShadowLayers,
        glm::value_ptr(pointMats[0]));
    shadowPointShader->setVec3Array("lightPositions", kMaxPointLights,
                                     glm::value_ptr(pointPositions[0]));
    shadowPointShader->setFloat("pointFarPlane", kPointShadowFar);
    renderEntities(*shadowPointShader, game, models, /*forShadowPass=*/true);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // Scene pass: render forward + skybox into the offscreen FBO.
  glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
  glViewport(0, 0, fbWidth, fbHeight);
  glEnable(GL_DEPTH_TEST);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  shader->use();
  setupCameraMatrix(*shader, *camera);
  shader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, dirShadowMap);
  shader->setInt("dirShadowMap", 4);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, pointShadowMaps);
  shader->setInt("pointShadowMaps", 5);
  shader->setFloat("pointFarPlane", kPointShadowFar);
  updateDirectionalLight(*shader, game);
  updatePointLights(*shader, game);
  renderEntities(*shader, game, models);

  auto* sceneInfo = currentScene(game);
  if (sceneInfo) {
    std::string skyboxDir = std::string(sceneInfo->skyboxDirectory);
    auto it = skyboxes.find(skyboxDir);
    if (it != skyboxes.end()) {
      drawSkybox(*skyboxShader, it->second, *camera, projection);
    }
  }

  // Present pass: blit sceneColor to the default framebuffer via fullscreen
  // triangle. Phases 4-6 will insert tonemap / FXAA / bloom composite stages
  // between the scene pass and the present.
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbWidth, fbHeight);
  glDisable(GL_DEPTH_TEST);
  glClear(GL_COLOR_BUFFER_BIT);
  if (presentShader && presentShader->valid()) {
    presentShader->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneColor);
    presentShader->setInt("src", 0);
    glBindVertexArray(fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
  }

  drawDebugOverlay();
}

void Graphics::swap() { glfwSwapBuffers(window); }

Graphics::~Graphics() {
  shader.reset();
  skyboxShader.reset();
  presentShader.reset();
  shadowDirShader.reset();
  shadowPointShader.reset();
  debugOverlay.reset();

  if (fullscreenVAO) {
    glDeleteVertexArrays(1, &fullscreenVAO);
    fullscreenVAO = 0;
  }
  if (sceneFBO) {
    glDeleteFramebuffers(1, &sceneFBO);
    sceneFBO = 0;
  }
  if (sceneColor) {
    glDeleteTextures(1, &sceneColor);
    sceneColor = 0;
  }
  if (sceneDepth) {
    glDeleteRenderbuffers(1, &sceneDepth);
    sceneDepth = 0;
  }
  if (dirShadowFBO) {
    glDeleteFramebuffers(1, &dirShadowFBO);
    dirShadowFBO = 0;
  }
  if (dirShadowMap) {
    glDeleteTextures(1, &dirShadowMap);
    dirShadowMap = 0;
  }
  if (pointShadowFBO) {
    glDeleteFramebuffers(1, &pointShadowFBO);
    pointShadowFBO = 0;
  }
  if (pointShadowMaps) {
    glDeleteTextures(1, &pointShadowMaps);
    pointShadowMaps = 0;
  }

  for (auto& [name, model] : models) {
    delete model;
  }
  models.clear();
  skyboxes.clear();

  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  glfwTerminate();
}
