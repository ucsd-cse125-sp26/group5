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

std::optional<CameraState> computeCamera(const ClientGame& game) {
  auto selfIt = game.entityMap.find(game.myEntityId);
  if (selfIt == game.entityMap.end() || !game.registry.valid(selfIt->second) ||
      !game.registry.all_of<shared::Position, shared::Camera>(selfIt->second)) {
    return std::nullopt;
  }
  const auto& p = game.registry.get<shared::Position>(selfIt->second);
  const auto& cam = game.registry.get<shared::Camera>(selfIt->second);

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
  auto sceneView = game.registry.view<shared::Scene>();
  for (auto ent : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(ent);
    auto* info = shared::findScene(scene.name);
    if (info) return info;
  }
  return nullptr;
}

static void updateDirectionalLight(const Shader& shader,
                                   const ClientGame& game) {
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
  auto lightView = game.registry.view<shared::PointLight>();
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
  auto view = game.registry
                  .view<shared::Entity, shared::Position, shared::RenderInfo>();
  for (auto ent : view) {
    auto& p = view.get<shared::Position>(ent);
    auto& renderInfo = view.get<shared::RenderInfo>(ent);
    auto& entity = view.get<shared::Entity>(ent);
    if (entity.id == game.myEntityId) {
      continue;
    }
    Model* modelAsset = models[renderInfo.modelName];
    glm::quat rotation = glm::quat(p.qw, p.qx, p.qy, p.qz);
    auto model = glm::identity<glm::mat4>();
    model = glm::translate(model, glm::vec3(p.x, p.y, p.z));
    model = glm::scale(model, glm::vec3(renderInfo.scale));
    model = model * glm::mat4_cast(rotation) *
            glm::mat4_cast(modelAsset->orientation);

    Draw(shader, *modelAsset, model);
  }
}

bool Graphics::load(int width, int height) {
  if (!glfwInit()) return false;

  glfwWindowHint(GLFW_SAMPLES, 4);
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

  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported())
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

  int version = gladLoadGL(glfwGetProcAddress);
  printf("GL %d.%d\n", GLAD_VERSION_MAJOR(version),
         GLAD_VERSION_MINOR(version));

  shader.emplace("shaders/vertex.glsl", "shaders/fragment.glsl");
  skyboxShader.emplace("shaders/vertex_skybox.glsl",
                       "shaders/fragment_skybox.glsl");

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

  for (const auto& sc : shared::SCENES) {
    std::string dir = std::string(sc.skyboxDirectory);
    if (skyboxes.find(dir) == skyboxes.end()) {
      skyboxes[dir] = loadSkybox(dir);
      printf("Loaded skybox: %s (%s)\n", std::string(sc.name).c_str(),
             dir.c_str());
    }
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);

  projection = glm::perspective(
      glm::radians(45.0f),
      static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.0f);

  shader->use();
  shader->setMat4("projection", projection);
  initPointLights(*shader);

  return true;
}

void Graphics::render(ClientGame& game) {
  SIMPLE_PROFILE_SCOPE("Render");
  auto camera = computeCamera(game);
  if (!camera) return;

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
}

void Graphics::swap() { glfwSwapBuffers(window); }

Graphics::~Graphics() {
  shader.reset();
  skyboxShader.reset();

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
