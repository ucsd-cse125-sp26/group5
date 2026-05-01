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
#include "shared/assets.h"
#include "shared/components.h"
#include "shared/map_format.h"
#include "shared/simple_profiler.h"

// Skybox images use Y-up; the game uses Z-up.
// cubemap->game: X->X, Y->Z, Z->-Y  (column-major)
static const glm::mat3 kCubemapToGame(1, 0, 0, 0, 0, 1, 0, -1, 0);

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
                           std::unordered_map<std::string, Model*>& models) {
  auto view = game.renderRegistry
                  .view<shared::Entity, shared::Position, shared::RenderInfo>();
  for (auto ent : view) {
    auto& p = view.get<shared::Position>(ent);
    auto& renderInfo = view.get<shared::RenderInfo>(ent);
    auto& entity = view.get<shared::Entity>(ent);
    if (entity.id == game.renderEntityId) {
      continue;
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
  debugOverlay.emplace("shaders/vertex_present.glsl",
                       "shaders/fragment_debug_overlay.glsl");

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
  // Phase 0: only DebugChannel::Off exists, so nothing to display. Later
  // phases bind their intermediate textures and configure the corner draw.
  if (debugChannel == DebugChannel::Off) return;
  // Reserved: bind the channel's texture, set viewport to a corner rect,
  // disable depth, glBindVertexArray(fullscreenVAO),
  // glDrawArrays(GL_TRIANGLES, 0, 3), restore viewport.
}

void Graphics::render(ClientGame& game) {
  SIMPLE_PROFILE_SCOPE("Render");
  auto camera = computeCamera(game);
  if (!camera) return;

  // Scene pass: render forward + skybox into the offscreen FBO.
  glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
  glViewport(0, 0, fbWidth, fbHeight);
  glEnable(GL_DEPTH_TEST);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  shader->use();
  setupCameraMatrix(*shader, *camera);
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
