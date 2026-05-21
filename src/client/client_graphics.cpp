// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
// clang-format on

#include "client_graphics.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "client/asset.h"
#include "client/ui_settings.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shared/assets.h"
#include "shared/components.h"
#include "shared/gpu_mem_profiler.h"
#include "shared/gpu_profiler.h"
#include "shared/map_format.h"
#include "shared/shader_constants.h"
#include "shared/simple_profiler.h"
#include "shared/util.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <stb_image.h>  // implementation lives in asset.cpp

// Skybox images are Y-up; the game is Z-up.
static const glm::mat3 kCubemapToGame(1, 0, 0, 0, 0, 1, 0, -1, 0);

// std140 layout — must match CameraBlock in the deferred-lighting,
// vertex_gbuffer, and ssao shaders.
struct alignas(16) CameraUBOData {
  glm::mat4 view;
  glm::mat4 projection;
  glm::mat4 lightSpaceMatrix;
  glm::vec3 viewPos;
  float pointFarPlane;
};
static_assert(sizeof(CameraUBOData) == 208);

static constexpr GLuint kCameraUBOBinding = 0;

static void bindCameraBlock(GLuint prog) {
  GLuint blockIdx = glGetUniformBlockIndex(prog, "CameraBlock");
  if (blockIdx != GL_INVALID_INDEX) {
    glUniformBlockBinding(prog, blockIdx, kCameraUBOBinding);
  }
}

using shared::kMaxLightingShaderLights;
using shared::kMaxPointLights;
using shared::kPointShadowLayers;
using shared::kPointShadowNear;

struct LightUpload {
  glm::vec3 position;
  float constant;
  float linear;
  float quadratic;
  glm::vec3 ambient;
  glm::vec3 diffuse;
  glm::vec3 specular;
  int shadowIdx;  // -1 = non-shadow-casting
};

static int collectPointLights(const ClientGame& game,
                              LightUpload out[kMaxLightingShaderLights]) {
  int count = 0;
  int shadowSlot = 0;
  auto view = game.renderRegistry.view<shared::PointLight>();
  for (auto ent : view) {
    if (count >= kMaxLightingShaderLights) break;
    const auto& pl = view.get<shared::PointLight>(ent);
    LightUpload& l = out[count++];
    l.position = glm::vec3(pl.px, pl.py, pl.pz);
    l.constant = pl.constant;
    l.linear = pl.linear;
    l.quadratic = pl.quadratic;
    l.ambient = glm::vec3(pl.ambientR, pl.ambientG, pl.ambientB);
    l.diffuse = glm::vec3(pl.diffuseR, pl.diffuseG, pl.diffuseB);
    l.specular = glm::vec3(pl.specularR, pl.specularG, pl.specularB);
    l.shadowIdx =
        (pl.castsShadow && shadowSlot < kMaxPointLights) ? shadowSlot++ : -1;
  }
  return count;
}

static void uploadPointLights(const Shader& shader, const LightUpload* lights,
                              int count) {
  shader.setInt("numPointLights", count);
  for (int i = 0; i < count; ++i) {
    std::string base = "pointLights[" + std::to_string(i) + "].";
    shader.setVec3(base + "position", lights[i].position);
    shader.setFloat(base + "constant", lights[i].constant);
    shader.setFloat(base + "linear", lights[i].linear);
    shader.setFloat(base + "quadratic", lights[i].quadratic);
    shader.setVec3(base + "ambient", lights[i].ambient);
    shader.setVec3(base + "diffuse", lights[i].diffuse);
    shader.setVec3(base + "specular", lights[i].specular);
    shader.setInt(base + "shadowIdx", lights[i].shadowIdx);
  }
}

static void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
  auto* g = static_cast<Graphics*>(glfwGetWindowUserPointer(w));
  if (g) g->resizeBuffers(width, height);
}

static void GLAPIENTRY glDebugCallback(GLenum /*source*/, GLenum type,
                                       GLuint /*id*/, GLenum severity,
                                       GLsizei /*length*/, const GLchar* msg,
                                       const void* /*userParam*/) {
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
  const char* sev = severity == GL_DEBUG_SEVERITY_HIGH     ? "HIGH"
                    : severity == GL_DEBUG_SEVERITY_MEDIUM ? "MED"
                    : severity == GL_DEBUG_SEVERITY_LOW    ? "LOW"
                                                           : "?";
  fprintf(stderr, "[GL %s%s] %s\n", sev,
          type == GL_DEBUG_TYPE_ERROR ? " ERROR" : "", msg);
}

// ECS DirectionalLight overrides the scene default; falls back to a fixed
// direction so the shadow pass still runs if neither is configured.
static glm::vec3 directionalLightDir(const ClientGame& game) {
  auto dlView = game.renderRegistry.view<shared::DirectionalLight>();
  for (auto ent : dlView) {
    const auto& dl = dlView.get<shared::DirectionalLight>(ent);
    return {dl.dirX, dl.dirY, dl.dirZ};
  }
  auto sceneView = game.renderRegistry.view<shared::Scene>();
  for (auto ent : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(ent);
    if (auto* info = shared::findScene(scene.name)) {
      return {info->dirX, info->dirY, info->dirZ};
    }
  }
  return {0.3f, 1.0f, -0.4f};
}

// Inactive shadow slots get a "kill" matrix that clips all geometry so the
// cleared depth=1.0 remains (sampled as "not in shadow").
static void computePointShadowMatrices(
    const LightUpload* lights, int count, float farPlane,
    glm::mat4 outMatrices[kPointShadowLayers],
    glm::vec3 outPositions[kMaxPointLights]) {
  static const struct {
    glm::vec3 dir;
    glm::vec3 up;
  } faces[6] = {
      {.dir = {1, 0, 0}, .up = {0, -1, 0}},
      {.dir = {-1, 0, 0}, .up = {0, -1, 0}},
      {.dir = {0, 1, 0}, .up = {0, 0, 1}},
      {.dir = {0, -1, 0}, .up = {0, 0, -1}},
      {.dir = {0, 0, 1}, .up = {0, -1, 0}},
      {.dir = {0, 0, -1}, .up = {0, -1, 0}},
  };

  glm::mat4 kill(0.0f);
  kill[3] = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
  for (int i = 0; i < kPointShadowLayers; ++i) outMatrices[i] = kill;
  for (int i = 0; i < kMaxPointLights; ++i) outPositions[i] = glm::vec3(0.0f);

  glm::mat4 proj =
      glm::perspective(glm::radians(90.0f), 1.0f, kPointShadowNear, farPlane);

  for (int i = 0; i < count; ++i) {
    int slot = lights[i].shadowIdx;
    if (slot < 0 || slot >= kMaxPointLights) continue;
    const glm::vec3& p = lights[i].position;
    outPositions[slot] = p;
    for (int f = 0; f < 6; ++f) {
      outMatrices[slot * 6 + f] =
          proj * glm::lookAt(p, p + faces[f].dir, faces[f].up);
    }
  }
}

// Camera-following ortho frustum. lightPos is texel-snapped to the light's
// tangent plane so static geometry doesn't shimmer as the camera moves.
static glm::mat4 computeDirectionalLightMatrix(const glm::vec3& cameraPos,
                                               const glm::vec3& lightDir,
                                               float halfExtent,
                                               float backDistance,
                                               float farPlane, int mapSize) {
  glm::vec3 dir = glm::normalize(lightDir);
  glm::vec3 up = glm::abs(dir.z) > 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                        : glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec3 right = glm::normalize(glm::cross(dir, up));
  glm::vec3 lightUp = glm::cross(right, dir);
  const float texelWorld = (2.0f * halfExtent) / static_cast<float>(mapSize);
  float u = glm::dot(cameraPos, right);
  float v = glm::dot(cameraPos, lightUp);
  u = std::floor(u / texelWorld) * texelWorld;
  v = std::floor(v / texelWorld) * texelWorld;
  glm::vec3 snapped = right * u + lightUp * v + dir * glm::dot(cameraPos, dir);
  glm::vec3 lightPos = snapped - dir * backDistance;
  glm::mat4 view = glm::lookAt(lightPos, snapped, up);
  glm::mat4 proj = glm::ortho(-halfExtent, halfExtent, -halfExtent, halfExtent,
                              1.0f, farPlane);
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
  if (!game.renderRegistry.all_of<shared::RenderInfo>(selfIt->second)) {
    return std::nullopt;
  }
  const auto& p = game.renderRegistry.get<shared::Position>(selfIt->second);
  const auto& cam = game.renderRegistry.get<shared::Camera>(selfIt->second);
  const auto& selfRender =
      game.renderRegistry.get<shared::RenderInfo>(selfIt->second);

  // Maze mode: detected by the replicated MazeSpiritGrid component, which
  // only exists on the maze world's spirit cube. Gating on modelName or a
  // mesh+scale fingerprint was unsafe — KEY_SWAP_MODEL flips the avatar's
  // mesh, and overworld decoration cubes can share the spirit cube's
  // scale.
  {
    entt::entity spirit = entt::null;
    auto spiritView =
        game.renderRegistry.view<shared::Position, shared::MazeSpiritGrid>();
    for (auto ent : spiritView) {
      spirit = ent;
      break;
    }
    if (spirit != entt::null) {
      const auto& sp = game.renderRegistry.get<shared::Position>(spirit);
      int slot = selfRender.playerSlot;
      if (slot < 1 || slot > 4) slot = 1;

      glm::vec3 side(0.0f);
      switch (slot) {
        case 1:
          side = glm::vec3(0.0f, 1.0f, 0.0f);
          break;
        case 2:
          side = glm::vec3(0.0f, -1.0f, 0.0f);
          break;
        case 3:
          side = glm::vec3(-1.0f, 0.0f, 0.0f);
          break;
        case 4:
          side = glm::vec3(1.0f, 0.0f, 0.0f);
          break;
      }

      glm::vec3 pos = glm::vec3(sp.x, sp.y, sp.z + 0.6f) + side * 0.55f;
      glm::mat4 view =
          glm::lookAt(pos, pos + side, glm::vec3(0.0f, 0.0f, 1.0f));
      return CameraState{.position = pos, .view = view};
    }
  }

  const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
  glm::quat playerRot(p.qw, p.qx, p.qy, p.qz);
  // Yaw-only so entity pitch/roll doesn't tilt the camera.
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

static const shared::SceneInfo* currentScene(const ClientGame& game) {
  auto sceneView = game.renderRegistry.view<shared::Scene>();
  for (auto ent : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(ent);
    auto* info = shared::findScene(scene.name);
    if (info) return info;
  }
  return nullptr;
}

// User override > ECS DirectionalLight > scene default. First entity wins.
static void uploadDirectionalLight(const Shader& shader, const ClientGame& game,
                                   const GraphicsSettings& settings) {
  if (settings.overrideDirLight) {
    shader.setVec3("dirLight.direction", settings.dirLightDirection);
    shader.setVec3("dirLight.ambient", settings.dirLightAmbient);
    shader.setVec3("dirLight.diffuse", settings.dirLightDiffuse);
    shader.setVec3("dirLight.specular", settings.dirLightSpecular);
    return;
  }
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

// Advances every entity's Animator and drops cache entries for entities
// that no longer exist or no longer reference a skinned model.
static void updateAnimators(Graphics& gfx, ClientGame& game, float dt) {
  auto& reg = game.renderRegistry;
  auto view = reg.view<shared::Entity, shared::RenderInfo>();
  for (auto ent : view) {
    auto& renderInfo = view.get<shared::RenderInfo>(ent);
    auto modelIt = gfx.models.find(renderInfo.modelName);
    if (modelIt == gfx.models.end() || !modelIt->second ||
        !modelIt->second->skinned) {
      gfx.animators.erase(ent);
      continue;
    }
    Model* modelAsset = modelIt->second;

    auto libIt = gfx.animationLibraries.find(renderInfo.modelName);
    if (libIt == gfx.animationLibraries.end()) {
      libIt = gfx.animationLibraries
                  .emplace(renderInfo.modelName,
                           std::make_unique<AnimationLibrary>(modelAsset))
                  .first;
    }
    if (libIt->second->empty()) {
      gfx.animators.erase(ent);
      continue;
    }

    std::string clipName;
    if (auto* state = reg.try_get<shared::AnimationState>(ent)) {
      clipName = state->clipName;
    }
    auto& animator = gfx.animators[ent];
    Animation* clip = libIt->second->find(clipName);
    if (clip && clip != animator.current()) {
      animator.play(clip);
    }

    // Look-pitch override on the neck bone. The override post-multiplies in
    // the bone's local space so the rest of the chain (head, hat, etc.)
    // inherits the rotation through the recursive walk. Pitch is negated
    // because the dog rig's neck-local X axis points opposite the camera's
    // pitch convention.
    animator.clearBoneOverrides();
    if (!modelAsset->neckBoneName.empty()) {
      if (auto* cam = reg.try_get<shared::Camera>(ent)) {
        glm::mat4 pitchM =
            glm::rotate(glm::mat4(1.0f), -cam->pitch, glm::vec3(1, 0, 0));
        animator.setBoneOverride(modelAsset->neckBoneName, pitchM);
      }
    }

    animator.update(dt);
  }
  // Drop animator entries for entities that were destroyed this frame.
  for (auto it = gfx.animators.begin(); it != gfx.animators.end();) {
    if (!reg.valid(it->first)) {
      it = gfx.animators.erase(it);
    } else {
      ++it;
    }
  }
}

static void renderEntities(const Shader& shader, Graphics& gfx,
                           ClientGame& game,
                           std::unordered_map<std::string, Model*>& models,
                           bool forShadowPass = false) {
  auto view = game.renderRegistry
                  .view<shared::Entity, shared::Position, shared::RenderInfo>();
  for (auto ent : view) {
    auto& p = view.get<shared::Position>(ent);
    auto& renderInfo = view.get<shared::RenderInfo>(ent);
    auto& entity = view.get<shared::Entity>(ent);
    // Light markers shouldn't shadow themselves.
    if (forShadowPass) {
      if (game.renderRegistry
              .any_of<shared::PointLight, shared::DirectionalLight>(ent))
        continue;
    }
    // Self entity is rendered in shadow passes (cast own shadow) but skipped
    // in the main pass (don't draw inside FP camera).
    if (!forShadowPass) {
      if (entity.id == game.renderEntityId) continue;
    }
    std::string modelKey = renderInfo.modelName;
    if (renderInfo.playerSlot >= 1 && renderInfo.playerSlot <= 4 &&
        renderInfo.modelName == "cube") {
      modelKey = "cube_slot" + std::to_string(renderInfo.playerSlot);
    }
    auto it = models.find(modelKey);
    Model* modelAsset = it != models.end() ? it->second : nullptr;
    if (!modelAsset) {
      auto fallbackIt = models.find(renderInfo.modelName);
      modelAsset = fallbackIt != models.end() ? fallbackIt->second : nullptr;
    }
    if (!modelAsset) continue;
    glm::quat rotation = glm::quat(p.qw, p.qx, p.qy, p.qz);
    auto model = glm::identity<glm::mat4>();
    model = glm::translate(model, glm::vec3(p.x, p.y, p.z));
    model = glm::scale(model,
                       glm::vec3(renderInfo.sx, renderInfo.sy, renderInfo.sz));
    model = model * glm::mat4_cast(rotation) *
            glm::mat4_cast(modelAsset->orientation);

    // Skinned path: hand the Animator's bone palette through to Draw, but
    // only as many matrices as this model actually uses. Cuts uniform
    // bandwidth from MAX_BONES*64B to boneCount*64B.
    const glm::mat4* bones = nullptr;
    int boneCount = 0;
    if (modelAsset->skinned) {
      auto animIt = gfx.animators.find(ent);
      if (animIt != gfx.animators.end()) {
        const auto& palette = animIt->second.finalBoneMatrices();
        bones = palette.data();
        boneCount = std::min(modelAsset->boneCount, MAX_BONES);
      }
    }
    Draw(shader, *modelAsset, model, bones, boneCount);
  }
}

bool Graphics::load(int width, int height) {
  if (!glfwInit()) return false;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  // Debug context — KHR_debug is core in 4.3 but widely available as an
  // extension on 4.1 (which we target for macOS support). We install the
  // callback below only if the loader picked up the function pointer.
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

  window = glfwCreateWindow(width, height, "Hello World", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported())
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

  glfwGetWindowPos(window, &windowedX, &windowedY);
  glfwGetWindowSize(window, &windowedW, &windowedH);
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

  int version = gladLoadGL(glfwGetProcAddress);
  printf("GL %d.%d\n", GLAD_VERSION_MAJOR(version),
         GLAD_VERSION_MINOR(version));

  if (glDebugMessageCallback) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugCallback, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                          GL_TRUE);
  }

  // Bring up ImGui + the tiny loading scene first so the user sees a
  // spinning cube + status text while the heavy asset loads run.
  initImGui();
  initLoadingScreen();
  renderLoadingFrame("Compiling shaders");

  gbufferShader.emplace("shaders/vertex_gbuffer.glsl",
                        "shaders/fragment_gbuffer.glsl");
  lightingShader.emplace("shaders/vertex_present.glsl",
                         "shaders/fragment_lighting_deferred.glsl");
  lightingCelShader.emplace("shaders/vertex_present.glsl",
                            "shaders/fragment_lighting_cel.glsl");
  outlineSobelShader.emplace("shaders/vertex_present.glsl",
                             "shaders/fragment_outline_sobel.glsl");
  skyboxShader.emplace("shaders/vertex_skybox.glsl",
                       "shaders/fragment_skybox.glsl");
  presentShader.emplace("shaders/vertex_present.glsl",
                        "shaders/fragment_fxaa.glsl");
  blurShader.emplace("shaders/vertex_present.glsl",
                     "shaders/fragment_blur.glsl");
  tonemapShader.emplace("shaders/vertex_present.glsl",
                        "shaders/fragment_tonemap.glsl");
  ssaoShader.emplace("shaders/vertex_present.glsl",
                     "shaders/fragment_ssao.glsl");
  ssaoBlurShader.emplace("shaders/vertex_present.glsl",
                         "shaders/fragment_ssao_blur.glsl");
  shadowDirShader.emplace("shaders/vertex_shadow_dir.glsl",
                          "shaders/fragment_shadow_dir.glsl");
  shadowPointShader.emplace("shaders/vertex_shadow_point.glsl",
                            "shaders/fragment_shadow_point.glsl");
  debugOverlay.emplace("shaders/vertex_present.glsl",
                       "shaders/fragment_debug_overlay.glsl");

  // Shader ctor failures are silent at runtime so F5 hot-reload can keep
  // the previous program — at startup there is no previous program, so
  // fail fast instead of starting with a black window.
  const std::optional<Shader>* required[] = {
      &gbufferShader,     &lightingShader,     &lightingCelShader,
      &outlineSobelShader, &skyboxShader,      &presentShader,
      &blurShader,        &tonemapShader,      &ssaoShader,
      &ssaoBlurShader,    &shadowDirShader,    &shadowPointShader,
      &debugOverlay,
  };
  for (const auto* s : required) {
    if (!*s || !(*s)->valid()) {
      fprintf(stderr, "Graphics::load: required shader failed to compile\n");
      return false;
    }
  }

  renderLoadingFrame("Allocating shadow maps");
  allocateDirShadowMap(settings.dirShadowMapSize);
  allocatePointShadowMaps(settings.pointShadowMapSize);
  lastDirShadowSize = settings.dirShadowMapSize;
  lastPointShadowSize = settings.pointShadowMapSize;

  // Empty VAO for fullscreen-triangle draws; positions synthesized from
  // gl_VertexID in vertex_present.glsl.
  glGenVertexArrays(1, &fullscreenVAO);

  glGenBuffers(1, &cameraUBO);
  glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraUBOData), nullptr,
               GL_DYNAMIC_DRAW);
  GPU_MEM_ADD("UBO", sizeof(CameraUBOData));
  glBindBufferBase(GL_UNIFORM_BUFFER, kCameraUBOBinding, cameraUBO);
  for (auto* s : {&*gbufferShader, &*lightingShader, &*ssaoShader}) {
    bindCameraBlock(s->id());
  }

  // SSAO: 64-sample hemisphere kernel biased towards the origin so closer
  // samples weight more heavily.
  {
    std::default_random_engine gen(0xc4b1u);
    std::uniform_real_distribution<float> rand01(0.0f, 1.0f);
    auto lerp = [](float a, float b, float t) { return a + t * (b - a); };
    ssaoKernel.clear();
    ssaoKernel.reserve(64);
    for (int i = 0; i < 64; ++i) {
      glm::vec3 sample(rand01(gen) * 2.0f - 1.0f, rand01(gen) * 2.0f - 1.0f,
                       rand01(gen));
      sample = glm::normalize(sample);
      sample *= rand01(gen);
      float scale = static_cast<float>(i) / 64.0f;
      sample *= lerp(0.1f, 1.0f, scale * scale);
      ssaoKernel.push_back(sample);
    }
    // 4×4 noise tile of tangent-space rotation vectors; z=0 so rotation is
    // around the surface normal.
    std::vector<glm::vec3> noise(16);
    for (int i = 0; i < 16; ++i) {
      noise[i] =
          glm::vec3(rand01(gen) * 2.0f - 1.0f, rand01(gen) * 2.0f - 1.0f, 0.0f);
    }
    glGenTextures(1, &ssaoNoiseTex);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT,
                 noise.data());
    GPU_MEM_TEX2D("SSAONoise", GL_RGB16F, 4, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }

  for (const auto& asset : shared::ASSETS) {
    renderLoadingFrame(std::string("Loading asset: ") +
                       std::string(asset.name));
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

  renderLoadingFrame("Building player-slot cubes");
  for (uint8_t s = 1; s <= 4; s++) {
    std::string name = "cube_slot" + std::to_string(s);
    Model* m = makePlayerSlotCubeModel(shared::CUBE_RAINBOW, s);
    if (m) {
      m->orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
      models[name] = m;
      printf("Loaded asset: %s (player join order)\n", name.c_str());
    }
  }

  // Per-node sub-models keyed to match RenderInfo.modelName from map_loader.
  renderLoadingFrame(std::string("Loading map: ") +
                     std::string(shared::DEFAULT_MAP_PATH));
  auto mapModels = loadMapModels(shared::DEFAULT_MAP_PATH);
  for (auto& [key, m] : mapModels) {
    models[key] = m;
    printf("Loaded map sub-model: %s\n", key.c_str());
  }

  for (const auto& sc : shared::SCENES) {
    std::string dir = std::string(sc.skyboxDirectory);
    if (skyboxes.find(dir) == skyboxes.end()) {
      renderLoadingFrame(std::string("Loading skybox: ") + dir);
      skyboxes[dir] = loadSkybox(dir);
      printf("Loaded skybox: %s (%s)\n", std::string(sc.name).c_str(),
             dir.c_str());
    }
  }

  glEnable(GL_DEPTH_TEST);

  renderLoadingFrame("Allocating framebuffers");
  resizeBuffers(fbWidth, fbHeight);
  initShaderUniforms();

  destroyLoadingScreen();
  return true;
}

void Graphics::resizeBuffers(int width, int height) {
  if (width <= 0 || height <= 0) return;
  fbWidth = width;
  fbHeight = height;
  glViewport(0, 0, fbWidth, fbHeight);
  // Projection is rebuilt every frame in render() from settings.

  // All offscreen FBOs render here; only the final present pass blits to the
  // full framebuffer (with GL_NEAREST when scale > 1 for chunky pixels).
  const int scale = std::max(1, settings.pixelationScale);
  renderWidth = std::max(1, fbWidth / scale);
  renderHeight = std::max(1, fbHeight / scale);
  lastPixelationScale = scale;
  const int rw = renderWidth;
  const int rh = renderHeight;

  // Same texture names get reallocated to the new dimensions, so reset the
  // categories before re-adding their byte sizes below.
  GPU_MEM_CLEAR("GBuffer");
  GPU_MEM_CLEAR("LitHDR");
  GPU_MEM_CLEAR("PingPong");
  GPU_MEM_CLEAR("SSAO");
  GPU_MEM_CLEAR("LDR");

  // gPosition: full FP32 — half-float quantizes distant world-space neighbors
  // to the same value, breaking SSAO. Normal fits in RGBA16F; albedo,
  // specular, and emissive fit in RGBA8.
  if (!gBufferFBO) glGenFramebuffers(1, &gBufferFBO);
  if (!gPosition) glGenTextures(1, &gPosition);
  if (!gNormal) glGenTextures(1, &gNormal);
  if (!gAlbedo) glGenTextures(1, &gAlbedo);
  if (!gSpecular) glGenTextures(1, &gSpecular);
  if (!gEmissive) glGenTextures(1, &gEmissive);
  if (!gBufferDepth) glGenRenderbuffers(1, &gBufferDepth);

  auto allocColor = [&](GLuint tex, GLint internalFmt, GLenum fmt,
                        GLenum type) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, rw, rh, 0, fmt, type,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  // RGBA32F: world position needs full FP32 precision so SSAO/shadows on
  // distant geometry don't quantize neighbors to the same value.
  allocColor(gPosition, GL_RGBA32F, GL_RGBA, GL_FLOAT);
  allocColor(gNormal, GL_RGBA16F, GL_RGBA, GL_FLOAT);
  allocColor(gAlbedo, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  allocColor(gSpecular, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  allocColor(gEmissive, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  GPU_MEM_TEX2D("GBuffer", GL_RGBA32F, rw, rh);
  GPU_MEM_TEX2D("GBuffer", GL_RGBA16F, rw, rh);
  GPU_MEM_TEX2D("GBuffer", GL_RGBA8, rw, rh);
  GPU_MEM_TEX2D("GBuffer", GL_RGBA8, rw, rh);
  GPU_MEM_TEX2D("GBuffer", GL_RGBA8, rw, rh);

  glBindRenderbuffer(GL_RENDERBUFFER, gBufferDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, rw, rh);
  GPU_MEM_RENDERBUFFER("GBuffer", GL_DEPTH32F_STENCIL8, rw, rh);

  glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         gPosition, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         gNormal, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D,
                         gAlbedo, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D,
                         gSpecular, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D,
                         gEmissive, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, gBufferDepth);
  GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                          GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,
                          GL_COLOR_ATTACHMENT4};
  glDrawBuffers(5, drawBuffers);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "gBufferFBO incomplete\n");
  }

  // litColor + brightColor: HDR MRT for deferred lighting; skybox writes
  // litColor only on the second pass.
  if (!litFBO) glGenFramebuffers(1, &litFBO);
  if (!litColor) glGenTextures(1, &litColor);
  if (!brightColor) glGenTextures(1, &brightColor);
  if (!litDepth) glGenRenderbuffers(1, &litDepth);

  auto allocHDR = [&](GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, rw, rh, 0, GL_RGBA,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  allocHDR(litColor);
  allocHDR(brightColor);
  GPU_MEM_TEX2D("LitHDR", GL_RGBA16F, rw, rh);
  GPU_MEM_TEX2D("LitHDR", GL_RGBA16F, rw, rh);

  glBindRenderbuffer(GL_RENDERBUFFER, litDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, rw, rh);
  GPU_MEM_RENDERBUFFER("LitHDR", GL_DEPTH32F_STENCIL8, rw, rh);

  glBindFramebuffer(GL_FRAMEBUFFER, litFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         litColor, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         brightColor, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, litDepth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "litFBO incomplete\n");
  }

  for (int i = 0; i < 2; ++i) {
    if (!pingFBO[i]) glGenFramebuffers(1, &pingFBO[i]);
    if (!pingColor[i]) glGenTextures(1, &pingColor[i]);
    allocHDR(pingColor[i]);
    GPU_MEM_TEX2D("PingPong", GL_RGBA16F, rw, rh);
    glBindFramebuffer(GL_FRAMEBUFFER, pingFBO[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           pingColor[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      fprintf(stderr, "pingFBO[%d] incomplete\n", i);
    }
  }

  auto allocSsao = [&](GLuint& fbo, GLuint& tex) {
    if (!fbo) glGenFramebuffers(1, &fbo);
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, rw, rh, 0, GL_RED,
                 GL_FLOAT, nullptr);
    GPU_MEM_TEX2D("SSAO", GL_R16F, rw, rh);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      fprintf(stderr, "ssao FBO incomplete\n");
    }
    // Clear to 1.0 (no occlusion) so the lighting pass reads valid values
    // before SSAO has run.
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  };
  allocSsao(ssaoFBO, ssaoColor);
  allocSsao(ssaoBlurFBO, ssaoBlurColor);

  // Final upscale filter: NEAREST when pixelating (chunky pixels), LINEAR
  // otherwise. FXAA's neighbor-sampling lands on texel centers either way so
  // it keeps working under NEAREST.
  const GLint upscaleFilter = scale > 1 ? GL_NEAREST : GL_LINEAR;

  if (!ldrFBO) glGenFramebuffers(1, &ldrFBO);
  if (!ldrColor) glGenTextures(1, &ldrColor);
  glBindTexture(GL_TEXTURE_2D, ldrColor);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  GPU_MEM_TEX2D("LDR", GL_RGBA8, rw, rh);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, upscaleFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, upscaleFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, ldrFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ldrColor, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "ldrFBO incomplete\n");
  }

  // Sobel outline output (chained after tonemap when enabled).
  GPU_MEM_CLEAR("Outline");
  if (!sobelFBO) glGenFramebuffers(1, &sobelFBO);
  if (!sobelColor) glGenTextures(1, &sobelColor);
  glBindTexture(GL_TEXTURE_2D, sobelColor);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  GPU_MEM_TEX2D("Outline", GL_RGBA8, rw, rh);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, upscaleFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, upscaleFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, sobelFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         sobelColor, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "sobelFBO incomplete\n");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Graphics::initShaderUniforms() {
  // Re-bind CameraBlock after hot-reload produces a fresh program object.
  for (auto* s : {&gbufferShader, &lightingShader, &lightingCelShader,
                  &outlineSobelShader, &ssaoShader}) {
    if (*s && (*s)->valid()) bindCameraBlock((*s)->id());
  }
}

void Graphics::reloadShaders() {
  struct Reload {
    std::optional<Shader>& slot;
    const char* vert;
    const char* frag;
    const char* geom;  // "" = no geometry stage
  };
  Reload reloads[] = {
      {.slot = gbufferShader,
       .vert = "shaders/vertex_gbuffer.glsl",
       .frag = "shaders/fragment_gbuffer.glsl",
       .geom = ""},
      {.slot = lightingShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_lighting_deferred.glsl",
       .geom = ""},
      {.slot = lightingCelShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_lighting_cel.glsl",
       .geom = ""},
      {.slot = outlineSobelShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_outline_sobel.glsl",
       .geom = ""},
      {.slot = skyboxShader,
       .vert = "shaders/vertex_skybox.glsl",
       .frag = "shaders/fragment_skybox.glsl",
       .geom = ""},
      {.slot = presentShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_fxaa.glsl",
       .geom = ""},
      {.slot = blurShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_blur.glsl",
       .geom = ""},
      {.slot = tonemapShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_tonemap.glsl",
       .geom = ""},
      {.slot = ssaoShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_ssao.glsl",
       .geom = ""},
      {.slot = ssaoBlurShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_ssao_blur.glsl",
       .geom = ""},
      {.slot = shadowDirShader,
       .vert = "shaders/vertex_shadow_dir.glsl",
       .frag = "shaders/fragment_shadow_dir.glsl",
       .geom = ""},
      {.slot = shadowPointShader,
       .vert = "shaders/vertex_shadow_point.glsl",
       .frag = "shaders/fragment_shadow_point.glsl",
       .geom = ""},
      {.slot = debugOverlay,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_debug_overlay.glsl",
       .geom = ""},
  };
  for (auto& r : reloads) {
    Shader candidate = (r.geom && *r.geom) ? Shader(r.vert, r.frag, r.geom)
                                           : Shader(r.vert, r.frag);
    if (candidate.valid()) {
      r.slot.emplace(std::move(candidate));
      printf("Reloaded: %s + %s%s%s\n", r.vert, r.frag,
             (r.geom && *r.geom) ? " + " : "", r.geom ? r.geom : "");
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
    case DebugChannel::DirShadowMap:
      name = "DirShadowMap";
      break;
    case DebugChannel::GPosition:
      name = "GPosition";
      break;
    case DebugChannel::GNormal:
      name = "GNormal";
      break;
    case DebugChannel::GAlbedo:
      name = "GAlbedo";
      break;
    case DebugChannel::GSpecular:
      name = "GSpecular";
      break;
    case DebugChannel::GEmissive:
      name = "GEmissive";
      break;
    case DebugChannel::Ssao:
      name = "SSAO";
      break;
    case DebugChannel::SsaoBlur:
      name = "SSAOBlur";
      break;
    case DebugChannel::LitColor:
      name = "LitColor";
      break;
    case DebugChannel::BrightColor:
      name = "BrightColor";
      break;
    case DebugChannel::LdrColor:
      name = "LdrColor";
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

  // mode: 0=direct rgb, 1=normal-vis, 2=HDR tonemap, 3=single R as gray.
  GLuint texToShow = 0;
  int mode = 0;
  switch (debugChannel) {
    case DebugChannel::DirShadowMap:
      texToShow = dirShadowMap;
      mode = 3;
      break;
    case DebugChannel::GPosition:
      texToShow = gPosition;
      mode = 2;
      break;
    case DebugChannel::GNormal:
      texToShow = gNormal;
      mode = 1;
      break;
    case DebugChannel::GAlbedo:
      texToShow = gAlbedo;
      mode = 0;
      break;
    case DebugChannel::GSpecular:
      texToShow = gSpecular;
      mode = 0;
      break;
    case DebugChannel::GEmissive:
      texToShow = gEmissive;
      mode = 0;
      break;
    case DebugChannel::Ssao:
      texToShow = ssaoColor;
      mode = 3;
      break;
    case DebugChannel::SsaoBlur:
      texToShow = ssaoBlurColor;
      mode = 3;
      break;
    case DebugChannel::LitColor:
      texToShow = litColor;
      mode = 2;
      break;
    case DebugChannel::BrightColor:
      texToShow = brightColor;
      mode = 2;
      break;
    case DebugChannel::LdrColor:
      texToShow = ldrColor;
      mode = 0;
      break;
    case DebugChannel::Off:
    case DebugChannel::Count:
      return;
  }
  if (!texToShow) return;

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
  // Shadow textures use COMPARE_REF_TO_TEXTURE for hardware PCF, but the
  // overlay samples as plain sampler2D — temporarily flip compare off.
  bool isDirShadow = (debugChannel == DebugChannel::DirShadowMap);
  if (isDirShadow) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
  }
  debugOverlay->setInt("src", 0);
  debugOverlay->setInt("mode", mode);
  glBindVertexArray(fullscreenVAO);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  if (isDirShadow) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                    GL_COMPARE_REF_TO_TEXTURE);
  }
  glViewport(0, 0, fbWidth, fbHeight);
}

void Graphics::render(ClientGame& game) {
  SIMPLE_PROFILE_SCOPE("Render");
  GPU_PROFILE_SCOPE("Render");
  auto camera = computeCamera(game);
  if (!camera) return;

  if (settings.dirShadowMapSize != lastDirShadowSize) {
    allocateDirShadowMap(settings.dirShadowMapSize);
    lastDirShadowSize = settings.dirShadowMapSize;
  }
  if (settings.pointShadowMapSize != lastPointShadowSize) {
    allocatePointShadowMaps(settings.pointShadowMapSize);
    lastPointShadowSize = settings.pointShadowMapSize;
  }
  if (std::max(1, settings.pixelationScale) != lastPixelationScale) {
    resizeBuffers(fbWidth, fbHeight);
  }
  if (settings.paletteQuantizeColors != lastPaletteColors) {
    const int colors =
        std::min(settings.paletteQuantizeColors, shared::kMaxPaletteColors);
    for (auto& [name, m] : models) {
      if (m) buildModelPalette(*m, colors);
    }
    lastPaletteColors = settings.paletteQuantizeColors;
  }
  if (settings.shadowsEnabled != prevShadowsEnabled) {
    if (!settings.shadowsEnabled) clearShadowMaps();
    prevShadowsEnabled = settings.shadowsEnabled;
  }

  // Advance per-entity animators using real wallclock dt (independent of the
  // server tick). Only animated, skinned entities pay any work here.
  const double now = glfwGetTime();
  const float dt = lastFrameTime == 0.0
                       ? 0.0f
                       : static_cast<float>(now - lastFrameTime);
  lastFrameTime = now;
  updateAnimators(*this, game, dt);

  projection = glm::perspective(
      glm::radians(settings.fovDegrees),
      static_cast<float>(fbWidth) / static_cast<float>(fbHeight),
      settings.nearPlane, settings.farPlane);

  // Collect lights up front so shadow passes and the lighting pass agree
  // on shadow-slot assignments.
  LightUpload lights[kMaxLightingShaderLights];
  int numLights = collectPointLights(game, lights);

  lightSpaceMatrix = computeDirectionalLightMatrix(
      camera->position, directionalLightDir(game), settings.dirShadowHalfExtent,
      settings.dirShadowBackDistance, settings.dirShadowFarPlane,
      settings.dirShadowMapSize);

  {
    CameraUBOData ubo{};
    ubo.view = camera->view;
    ubo.projection = projection;
    ubo.lightSpaceMatrix = lightSpaceMatrix;
    ubo.viewPos = camera->position;
    ubo.pointFarPlane = settings.pointShadowFarPlane;
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ubo), &ubo);
  }
  if (settings.shadowsEnabled && shadowDirShader && shadowDirShader->valid()) {
    SIMPLE_PROFILE_SCOPE("ShadowDir");
    GPU_PROFILE_SCOPE("ShadowDir");
    glBindFramebuffer(GL_FRAMEBUFFER, dirShadowFBO);
    glViewport(0, 0, settings.dirShadowMapSize, settings.dirShadowMapSize);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    // Polygon offset only; front-face culling here causes peter-panning
    // on the single-sided floor/walls.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(settings.dirShadowPolyFactor, settings.dirShadowPolyUnits);
    shadowDirShader->use();
    shadowDirShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    shadowDirShader->setFloat("alphaCutoff", settings.shadowAlphaCutoff);
    renderEntities(*shadowDirShader, *this, game, models,
                   /*forShadowPass=*/true);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // Point shadows: one depth pass per active light × 6 cube faces. Each
  // iteration rebinds the FBO depth attachment to a single cubemap-array
  // layer. Avoids the geometry-shader expansion that dominated GPU time
  // when this was a single layered draw.
  glm::mat4 pointMats[kPointShadowLayers];
  glm::vec3 pointPositions[kMaxPointLights];
  computePointShadowMatrices(lights, numLights, settings.pointShadowFarPlane,
                             pointMats, pointPositions);
  if (settings.shadowsEnabled && shadowPointShader &&
      shadowPointShader->valid() && numLights > 0) {
    SIMPLE_PROFILE_SCOPE("ShadowPoint");
    GPU_PROFILE_SCOPE("ShadowPoint");
    glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
    glViewport(0, 0, settings.pointShadowMapSize, settings.pointShadowMapSize);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(settings.pointShadowPolyFactor,
                    settings.pointShadowPolyUnits);
    shadowPointShader->use();
    shadowPointShader->setFloat("pointFarPlane", settings.pointShadowFarPlane);
    shadowPointShader->setFloat("alphaCutoff", settings.shadowAlphaCutoff);
    for (int i = 0; i < numLights; ++i) {
      int slot = lights[i].shadowIdx;
      if (slot < 0 || slot >= kMaxPointLights) continue;
      shadowPointShader->setVec3("lightPos", lights[i].position);
      for (int f = 0; f < 6; ++f) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  pointShadowMaps, 0, slot * 6 + f);
        glClear(GL_DEPTH_BUFFER_BIT);
        shadowPointShader->setMat4("lightSpaceMatrix", pointMats[slot * 6 + f]);
        renderEntities(*shadowPointShader, *this, game, models,
                       /*forShadowPass=*/true);
      }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  {
    SIMPLE_PROFILE_SCOPE("GBuffer");
    GPU_PROFILE_SCOPE("GBuffer");
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
    glViewport(0, 0, renderWidth, renderHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // gPosition.a = 0 marks "no geometry written" so the skybox can take
    // those pixels.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gbufferShader->use();
    gbufferShader->setInt("textureQuantizeLevels",
                          settings.textureQuantizeLevels);
    // paletteSize + palette uniforms are bound per Model inside Draw().
    renderEntities(*gbufferShader, *this, game, models);
  }

  if (settings.ssaoEnabled && ssaoShader && ssaoShader->valid()) {
    SIMPLE_PROFILE_SCOPE("SSAO");
    GPU_PROFILE_SCOPE("SSAO");
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
    glViewport(0, 0, renderWidth, renderHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoShader->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    ssaoShader->setInt("gPosition", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    ssaoShader->setInt("gNormal", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex);
    ssaoShader->setInt("texNoise", 2);
    ssaoShader->setVec3Array("samples", static_cast<int>(ssaoKernel.size()),
                             glm::value_ptr(ssaoKernel[0]));
    ssaoShader->setInt("kernelSize", settings.ssaoKernelSize);
    ssaoShader->setFloat("radius", settings.ssaoRadius);
    ssaoShader->setFloat("bias", settings.ssaoBias);
    ssaoShader->setVec2("noiseScale",
                        static_cast<float>(renderWidth) / 4.0f,
                        static_cast<float>(renderHeight) / 4.0f);
    glBindVertexArray(fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
  } else if (!settings.ssaoEnabled) {
    // Clear blurred SSAO to 1.0 so the lighting pass reads "no occlusion".
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
    glViewport(0, 0, renderWidth, renderHeight);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  if (settings.ssaoEnabled && ssaoBlurShader && ssaoBlurShader->valid()) {
    SIMPLE_PROFILE_SCOPE("SSAOBlur");
    GPU_PROFILE_SCOPE("SSAOBlur");
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
    glViewport(0, 0, renderWidth, renderHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoBlurShader->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssaoColor);
    ssaoBlurShader->setInt("src", 0);
    glBindVertexArray(fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
  }

  {
    SIMPLE_PROFILE_SCOPE("Lighting");
    GPU_PROFILE_SCOPE("Lighting");
    glBindFramebuffer(GL_FRAMEBUFFER, litFBO);
    GLenum litDrawBufs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, litDrawBufs);
    glViewport(0, 0, renderWidth, renderHeight);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    bool useCel = settings.shadingMode == ShadingMode::Cel &&
                  lightingCelShader && lightingCelShader->valid();
    Shader* lighting = useCel ? &*lightingCelShader : &*lightingShader;

    if (lighting && lighting->valid()) {
      if (useCel) ensureCelRampLoaded();
      lighting->use();
      lighting->setFloat(
          "bloomThreshold",
          settings.bloomEnabled ? settings.bloomThreshold : 1e9f);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, gPosition);
      lighting->setInt("gPosition", 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, gNormal);
      lighting->setInt("gNormal", 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, gAlbedo);
      lighting->setInt("gAlbedo", 2);
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, gSpecular);
      lighting->setInt("gSpecular", 3);
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, gEmissive);
      lighting->setInt("gEmissive", 4);
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D, ssaoBlurColor);
      lighting->setInt("ssao", 5);
      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_2D, dirShadowMap);
      lighting->setInt("dirShadowMap", 6);
      glActiveTexture(GL_TEXTURE7);
      glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, pointShadowMaps);
      lighting->setInt("pointShadowMaps", 7);
      if (useCel) {
        // Cel ramp on TEXTURE8 (bound to a 1×1 white fallback if no ramp).
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, celRampTexture);
        lighting->setInt("celRamp", 8);
        lighting->setInt("celBands", settings.celBands);
        lighting->setFloat("celBandEpsilon", settings.celBandEpsilon);
        lighting->setInt("halfLambert", settings.celHalfLambert ? 1 : 0);
        lighting->setFloat("celSpecularThreshold",
                           settings.celSpecularThreshold);
        lighting->setFloat("celSpecularEpsilon", settings.celSpecularEpsilon);
        lighting->setInt("useRampTexture",
                         (settings.celUseRampTexture && celRampTexture) ? 1 : 0);
      }
      uploadDirectionalLight(*lighting, game, settings);
      uploadPointLights(*lighting, lights, numLights);
      const bool cross = settings.outlineMode == OutlineMode::Cross;
      lighting->setInt("outlineCross", cross ? 1 : 0);
      if (cross) {
        lighting->setVec3("outlineColor", settings.outlineColor);
        lighting->setFloat("outlineDepthThreshold",
                           settings.outlineDepthThreshold);
        lighting->setFloat("outlineNormalThreshold",
                           settings.outlineNormalThreshold);
      }
      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
    }
  }

  // Blit g-buffer depth into litFBO so the skybox depth-tests against
  // scene geometry. Skybox writes litColor only (no bloom).
  {
    SIMPLE_PROFILE_SCOPE("Skybox");
    GPU_PROFILE_SCOPE("Skybox");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, litFBO);
    glBlitFramebuffer(0, 0, renderWidth, renderHeight,
                      0, 0, renderWidth, renderHeight,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, litFBO);
    GLenum skyDrawBufs[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, skyDrawBufs);
    glViewport(0, 0, renderWidth, renderHeight);
    glEnable(GL_DEPTH_TEST);
    auto* sceneInfo = currentScene(game);
    if (sceneInfo) {
      std::string skyboxDir = std::string(sceneInfo->skyboxDirectory);
      auto it = skyboxes.find(skyboxDir);
      if (it != skyboxes.end()) {
        drawSkybox(*skyboxShader, it->second, *camera, projection);
      }
    }
  }

  GLuint finalBloomColor = brightColor;
  const int effectiveBloomIters =
      settings.bloomEnabled ? settings.bloomBlurIterations : 0;
  if (blurShader && blurShader->valid() && effectiveBloomIters > 0) {
    SIMPLE_PROFILE_SCOPE("Bloom");
    GPU_PROFILE_SCOPE("Bloom");
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, renderWidth, renderHeight);
    blurShader->use();
    bool horizontal = true;
    bool firstIter = true;
    for (int i = 0; i < effectiveBloomIters; ++i) {
      int dst = horizontal ? 0 : 1;
      glBindFramebuffer(GL_FRAMEBUFFER, pingFBO[dst]);
      blurShader->setInt("horizontal", horizontal ? 1 : 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D,
                    firstIter ? brightColor : pingColor[1 - dst]);
      blurShader->setInt("src", 0);
      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
      finalBloomColor = pingColor[dst];
      horizontal = !horizontal;
      firstIter = false;
    }
  }

  {
    SIMPLE_PROFILE_SCOPE("Tonemap");
    GPU_PROFILE_SCOPE("Tonemap");
    glBindFramebuffer(GL_FRAMEBUFFER, ldrFBO);
    glViewport(0, 0, renderWidth, renderHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    if (tonemapShader && tonemapShader->valid()) {
      tonemapShader->use();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, litColor);
      tonemapShader->setInt("hdrColor", 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, finalBloomColor);
      tonemapShader->setInt("bloomColor", 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, gPosition);
      tonemapShader->setInt("gPosition", 2);
      tonemapShader->setFloat("exposure", settings.exposure);
      tonemapShader->setFloat(
          "bloomStrength",
          settings.bloomEnabled ? settings.bloomStrength : 0.0f);

      // Color restoration: pull the box from the local player's replicated
      // ColorBoundingBox. Strength=0 short-circuits the shader's effect path.
      float restorationStrength = 0.0f;
      glm::vec3 restoreMin(0.0f);
      glm::vec3 restoreMax(0.0f);
      if (settings.colorRestorationEnabled) {
        auto selfIt = game.renderEntityMap.find(game.renderEntityId);
        if (selfIt != game.renderEntityMap.end() &&
            game.renderRegistry.valid(selfIt->second) &&
            game.renderRegistry.all_of<shared::ColorBoundingBox>(
                selfIt->second)) {
          const auto& b = game.renderRegistry.get<shared::ColorBoundingBox>(
              selfIt->second);
          restoreMin = glm::vec3(b.minX, b.minY, b.minZ);
          restoreMax = glm::vec3(b.maxX, b.maxY, b.maxZ);
          restorationStrength = settings.colorRestorationStrength;
        }
      }
      tonemapShader->setFloat("colorRestorationStrength", restorationStrength);
      tonemapShader->setFloat("colorRestorationEdgeWidth",
                              settings.colorRestorationEdgeWidth);
      tonemapShader->setVec3("colorRestorationMin", restoreMin);
      tonemapShader->setVec3("colorRestorationMax", restoreMax);

      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
    }
  }

  GLuint finalLDR = ldrColor;
  if (settings.outlineMode == OutlineMode::Sobel &&
      outlineSobelShader && outlineSobelShader->valid()) {
    SIMPLE_PROFILE_SCOPE("OutlineSobel");
    GPU_PROFILE_SCOPE("OutlineSobel");
    glBindFramebuffer(GL_FRAMEBUFFER, sobelFBO);
    glViewport(0, 0, renderWidth, renderHeight);
    glDisable(GL_DEPTH_TEST);
    outlineSobelShader->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ldrColor);
    outlineSobelShader->setInt("src", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    outlineSobelShader->setInt("gNormal", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    outlineSobelShader->setInt("gPosition", 2);
    outlineSobelShader->setFloat("outlineSobelWidth", settings.outlineSobelWidth);
    outlineSobelShader->setFloat("outlineDepthThreshold",
                                  settings.outlineDepthThreshold);
    outlineSobelShader->setFloat("outlineNormalThreshold",
                                  settings.outlineNormalThreshold);
    outlineSobelShader->setVec3("outlineColor", settings.outlineColor);
    glBindVertexArray(fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    finalLDR = sobelColor;
  }

  {
    SIMPLE_PROFILE_SCOPE("Present");
    GPU_PROFILE_SCOPE("Present");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbWidth, fbHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    if (presentShader && presentShader->valid()) {
      presentShader->use();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, finalLDR);
      presentShader->setInt("src", 0);
      presentShader->setInt("fxaaEnabled", settings.fxaaEnabled ? 1 : 0);
      presentShader->setInt("postQuantizeLevels", settings.postQuantizeLevels);
      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
    }
  }

  drawDebugOverlay();

  drawSettingsUIFrame();
}

void Graphics::swap() { glfwSwapBuffers(window); }

Graphics::~Graphics() {
  shutdownImGui();
  // Renderer isn't recycled at runtime, so per-handle glDelete* would be
  // dead code. Tearing down GLFW is enough.
  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  glfwTerminate();
}

void Graphics::allocateDirShadowMap(int size) {
  if (!dirShadowFBO) glGenFramebuffers(1, &dirShadowFBO);
  if (!dirShadowMap) glGenTextures(1, &dirShadowMap);
  glBindTexture(GL_TEXTURE_2D, dirShadowMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  GPU_MEM_CLEAR("ShadowDir");
  GPU_MEM_TEX2D("ShadowDir", GL_DEPTH_COMPONENT24, size, size);
  // LINEAR + COMPARE_REF_TO_TEXTURE → 2×2 hardware PCF per tap.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                  GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  // Outside the frustum returns visibility=1.0 (fully lit).
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
  // Initialize to depth=1.0 so a sample before the first shadow pass returns
  // "fully lit" instead of undefined.
  glClear(GL_DEPTH_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Graphics::allocatePointShadowMaps(int size) {
  if (!pointShadowFBO) glGenFramebuffers(1, &pointShadowFBO);
  if (!pointShadowMaps) glGenTextures(1, &pointShadowMaps);
  glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, pointShadowMaps);
  glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT24, size, size,
               kPointShadowLayers, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  GPU_MEM_CLEAR("ShadowPoint");
  GPU_MEM_TEX3D("ShadowPoint", GL_DEPTH_COMPONENT24, size, size,
                kPointShadowLayers);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S,
                  GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T,
                  GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R,
                  GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_MODE,
                  GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
  // Bind layer 0 first so the FBO is complete; the render loop rebinds each
  // layer as it draws.
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, pointShadowMaps,
                            0, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "pointShadowFBO incomplete\n");
  }
  // Clear every layer to depth=1.0 so unused / disabled-shadow slots sample
  // as "fully lit".
  for (int layer = 0; layer < kPointShadowLayers; ++layer) {
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              pointShadowMaps, 0, layer);
    glClear(GL_DEPTH_BUFFER_BIT);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Graphics::clearShadowMaps() {
  if (dirShadowFBO) {
    glBindFramebuffer(GL_FRAMEBUFFER, dirShadowFBO);
    glViewport(0, 0, lastDirShadowSize, lastDirShadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
  }
  if (pointShadowFBO) {
    glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
    glViewport(0, 0, lastPointShadowSize, lastPointShadowSize);
    for (int layer = 0; layer < kPointShadowLayers; ++layer) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                pointShadowMaps, 0, layer);
      glClear(GL_DEPTH_BUFFER_BIT);
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Graphics::ensureCelRampLoaded() {
  if (settings.celRampPath == lastCelRampPath && celRampTexture != 0) return;
  lastCelRampPath = settings.celRampPath;

  // Always allocate the texture handle so the cel shader has something to
  // sample, even when the user hasn't picked a ramp yet. Fallback is a 1×1
  // identity ramp (sample == nDotL), so sampling acts like no quantization.
  if (!celRampTexture) glGenTextures(1, &celRampTexture);
  glBindTexture(GL_TEXTURE_2D, celRampTexture);

  bool loaded = false;
  if (!settings.celRampPath.empty()) {
    std::filesystem::path path =
        std::filesystem::path(exeDir()) / settings.celRampPath;
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels =
        stbi_load(path.string().c_str(), &w, &h, &ch, 3);
    if (pixels && w > 0 && h > 0) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
                   pixels);
      stbi_image_free(pixels);
      loaded = true;
    } else {
      fprintf(stderr, "ensureCelRampLoaded: failed to load %s\n",
              path.string().c_str());
    }
  }
  if (!loaded) {
    unsigned char identity[3] = {255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 identity);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Graphics::initImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // No imgui.ini side-file.
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 410 core");
}

void Graphics::initLoadingScreen() {
  loadingShader.emplace("shaders/vertex_loading.glsl",
                        "shaders/fragment_loading.glsl");
  if (!loadingShader || !loadingShader->valid()) {
    fprintf(stderr, "initLoadingScreen: shader failed to compile\n");
    return;
  }

  // 24-vertex cube with face normals — six quads, two triangles each.
  // Pos + normal interleaved (6 floats per vertex).
  struct Face {
    glm::vec3 normal;
    glm::vec3 corners[4];
  };
  const Face faces[6] = {
      {.normal = {0, 0, 1},
       .corners = {{-0.5f, -0.5f, 0.5f},
                   {0.5f, -0.5f, 0.5f},
                   {0.5f, 0.5f, 0.5f},
                   {-0.5f, 0.5f, 0.5f}}},
      {.normal = {0, 0, -1},
       .corners = {{0.5f, -0.5f, -0.5f},
                   {-0.5f, -0.5f, -0.5f},
                   {-0.5f, 0.5f, -0.5f},
                   {0.5f, 0.5f, -0.5f}}},
      {.normal = {1, 0, 0},
       .corners = {{0.5f, -0.5f, 0.5f},
                   {0.5f, -0.5f, -0.5f},
                   {0.5f, 0.5f, -0.5f},
                   {0.5f, 0.5f, 0.5f}}},
      {.normal = {-1, 0, 0},
       .corners = {{-0.5f, -0.5f, -0.5f},
                   {-0.5f, -0.5f, 0.5f},
                   {-0.5f, 0.5f, 0.5f},
                   {-0.5f, 0.5f, -0.5f}}},
      {.normal = {0, 1, 0},
       .corners = {{-0.5f, 0.5f, 0.5f},
                   {0.5f, 0.5f, 0.5f},
                   {0.5f, 0.5f, -0.5f},
                   {-0.5f, 0.5f, -0.5f}}},
      {.normal = {0, -1, 0},
       .corners = {{-0.5f, -0.5f, -0.5f},
                   {0.5f, -0.5f, -0.5f},
                   {0.5f, -0.5f, 0.5f},
                   {-0.5f, -0.5f, 0.5f}}},
  };

  std::vector<float> verts;
  verts.reserve(6 * 4 * 6);
  std::vector<GLuint> idx;
  idx.reserve(36);
  for (int f = 0; f < 6; ++f) {
    const auto base = static_cast<GLuint>(verts.size() / 6);
    for (int c = 0; c < 4; ++c) {
      verts.push_back(faces[f].corners[c].x);
      verts.push_back(faces[f].corners[c].y);
      verts.push_back(faces[f].corners[c].z);
      verts.push_back(faces[f].normal.x);
      verts.push_back(faces[f].normal.y);
      verts.push_back(faces[f].normal.z);
    }
    idx.push_back(base + 0);
    idx.push_back(base + 1);
    idx.push_back(base + 2);
    idx.push_back(base + 0);
    idx.push_back(base + 2);
    idx.push_back(base + 3);
  }

  glGenVertexArrays(1, &loadingCubeVAO);
  glGenBuffers(1, &loadingCubeVBO);
  glGenBuffers(1, &loadingCubeEBO);
  glBindVertexArray(loadingCubeVAO);
  glBindBuffer(GL_ARRAY_BUFFER, loadingCubeVBO);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
               verts.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, loadingCubeEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(idx.size() * sizeof(GLuint)),
               idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void*)(3 * sizeof(float)));
  glBindVertexArray(0);
  loadingCubeIndexCount = static_cast<int>(idx.size());
  loadingStartTime = glfwGetTime();
}

void Graphics::destroyLoadingScreen() {
  if (loadingCubeVAO) glDeleteVertexArrays(1, &loadingCubeVAO);
  if (loadingCubeVBO) glDeleteBuffers(1, &loadingCubeVBO);
  if (loadingCubeEBO) glDeleteBuffers(1, &loadingCubeEBO);
  loadingCubeVAO = loadingCubeVBO = loadingCubeEBO = 0;
  loadingCubeIndexCount = 0;
  loadingShader.reset();
}

void Graphics::renderLoadingFrame(const std::string& status) {
  if (!window || !loadingShader || !loadingShader->valid() ||
      !loadingCubeIndexCount) {
    return;
  }
  glfwPollEvents();
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
  if (fbWidth <= 0 || fbHeight <= 0) return;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbWidth, fbHeight);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDisable(GL_CULL_FACE);
  glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const float aspect = static_cast<float>(fbWidth) /
                       static_cast<float>(std::max(1, fbHeight));
  const float t = static_cast<float>(glfwGetTime() - loadingStartTime);
  glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
  glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                               glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  glm::mat4 model = glm::rotate(glm::mat4(1.0f), t * 1.2f,
                                glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
  glm::mat4 mvp = proj * view * model;
  glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

  loadingShader->use();
  loadingShader->setMat4("mvp", mvp);
  loadingShader->setMat3("normalMatrix", normalMatrix);
  glBindVertexArray(loadingCubeVAO);
  glDrawElements(GL_TRIANGLES, loadingCubeIndexCount, GL_UNSIGNED_INT,
                 nullptr);
  glBindVertexArray(0);

  if (ImGui::GetCurrentContext()) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    const float pad = 24.0f;
    ImGui::SetNextWindowPos(ImVec2(pad, io.DisplaySize.y - pad),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                              ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoNav |
                              ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoInputs |
                              ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##LoadingStatus", nullptr, flags)) {
      ImGui::TextUnformatted("Loading...");
      ImGui::Separator();
      ImGui::TextUnformatted(status.c_str());
    }
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  glfwSwapBuffers(window);
}

void Graphics::shutdownImGui() {
  if (!ImGui::GetCurrentContext()) return;
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void Graphics::drawSettingsUIFrame() {
  if (!ImGui::GetCurrentContext()) return;
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  if (settingsMenuOpen) drawSettingsUI(settings, settingsMenuOpen);
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
