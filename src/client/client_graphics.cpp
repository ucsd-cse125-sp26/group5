// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
// clang-format on

#include "client_graphics.h"

#include <stb_image.h>  // implementation lives in asset.cpp

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "client/asset.h"
#include "client/client_game.h"
#include "client/puzzle_hud.h"
#include "client/ui_settings.h"
#include "client_network.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "imgui.h"
#include "shared/assets.h"
#include "shared/components.h"
#include "shared/dev_spawn.h"
#include "shared/gpu_mem_profiler.h"
#include "shared/gpu_profiler.h"
#include "shared/map_format.h"
#include "shared/mesh_loader.h"
#include "shared/puzzles/maze/layout.h"
#include "shared/puzzles/tangram/defaults.h"
#include "shared/puzzles/tangram/puzzle_data.h"
#include "shared/puzzles/tangram/roles.h"
#include "shared/shader_constants.h"
#include "shared/simple_profiler.h"
#include "shared/util.h"

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

// Fully-saturated, full-value RGB for hue in [0, 1) — the HSV(h, 1, 1) edge.
static glm::vec3 hueToRgb(float hue) {
  float r = std::fabs(hue * 6.0f - 3.0f) - 1.0f;
  float g = 2.0f - std::fabs(hue * 6.0f - 2.0f);
  float b = 2.0f - std::fabs(hue * 6.0f - 4.0f);
  return glm::clamp(glm::vec3(r, g, b), 0.0f, 1.0f);
}

// Only fragments act as point lights — every other point light (the demo
// "spinning" light, map-authored lights) is intentionally skipped so the
// scene's only point lights are the fragments. Their color cycles through the
// rainbow over time, phased per entity. With kMaxPointLights == 1, just one
// fragment casts point shadows.
static int collectPointLights(const ClientGame& game,
                              LightUpload out[kMaxLightingShaderLights]) {
  int count = 0;
  int shadowSlot = 0;
  const auto now = static_cast<float>(glfwGetTime());
  auto view = game.renderRegistry.view<shared::PointLight>();
  for (auto ent : view) {
    if (count >= kMaxLightingShaderLights) break;
    const auto* ri = game.renderRegistry.try_get<shared::RenderInfo>(ent);
    if (!ri || ri->modelName != "fragment") continue;
    const auto& pl = view.get<shared::PointLight>(ent);
    LightUpload& l = out[count++];
    l.position = glm::vec3(pl.px, pl.py, pl.pz);
    l.constant = pl.constant;
    l.linear = pl.linear;
    l.quadratic = pl.quadratic;
    const auto* eid = game.renderRegistry.try_get<shared::Entity>(ent);
    float phase = eid ? static_cast<float>(eid->id & 0xFFu) / 255.0f : 0.0f;
    constexpr float kFragmentLightBrightness = 2.0f;
    glm::vec3 c =
        hueToRgb(glm::fract(now * 0.12f + phase)) * kFragmentLightBrightness;
    l.ambient = c * 0.1f;
    l.diffuse = c;
    l.specular = c;
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

static std::optional<CameraState> tangramLobbyFallbackCamera(
    const ClientGame& game) {
  const shared::tangram::ArenaLayout& layout = game.tangramArena;
  const float topZ = layout.platformTopZ();
  uint8_t slot = localOverworldPlayerSlot(game);
  if (slot < 1 || slot > 4) slot = 1;
  const int idx = static_cast<int>(slot) - 1;
  const glm::vec3 focus(layout.spawnBaseX + layout.spawnOffsetX[idx],
                        layout.spawnBaseY + layout.spawnOffsetY[idx],
                        topZ + 1.0f);
  const glm::vec3 eye(focus.x, focus.y - 5.5f, topZ + 4.0f);
  const glm::vec3 target(layout.lookAtX(), layout.lookAtY(), topZ + 0.5f);
  return CameraState{
      .position = eye,
      .view = glm::lookAt(eye, target, glm::vec3(0.0f, 0.0f, 1.0f))};
}

static std::optional<CameraState> overworldHubFallbackCamera(
    const ClientGame& game) {
  if (isOverworldTangramPuzzleActive(game) ||
      shared::dev_spawn::kOverworldSpawn ==
          shared::dev_spawn::OverworldSpawn::Tangram) {
    return tangramLobbyFallbackCamera(game);
  }
  const shared::maze_layout::Config& layout = game.mazeLayout;
  uint8_t slot = localOverworldPlayerSlot(game);
  if (slot < 1 || slot > 4) slot = 1;
  const int idx = static_cast<int>(slot) - 1;
  const float spawnZ = layout.spawnHeightZ;
  const glm::vec3 focus(layout.spawnBaseX + layout.spawnOffsetX[idx],
                        layout.spawnBaseY + layout.spawnOffsetY[idx],
                        spawnZ + 1.0f);
  const glm::vec3 eye(focus.x, focus.y - 5.5f, spawnZ + 4.0f);
  const glm::vec3 target(layout.lookAtX(), layout.lookAtY(), layout.lookAtZ());
  return CameraState{
      .position = eye,
      .view = glm::lookAt(eye, target, glm::vec3(0.0f, 0.0f, 1.0f))};
}

std::optional<CameraState> computeCamera(const ClientGame& game) {
  auto selfIt = game.renderEntityMap.find(game.renderEntityId);
  if (selfIt == game.renderEntityMap.end() ||
      !game.renderRegistry.valid(selfIt->second) ||
      !game.renderRegistry.all_of<shared::Position, shared::Camera>(
          selfIt->second)) {
    return overworldHubFallbackCamera(game);
  }
  if (!game.renderRegistry.all_of<shared::RenderInfo>(selfIt->second)) {
    return overworldHubFallbackCamera(game);
  }
  const auto& p = game.renderRegistry.get<shared::Position>(selfIt->second);
  const auto& cam = game.renderRegistry.get<shared::Camera>(selfIt->second);

  const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
  glm::vec3 pos = glm::vec3(p.x, p.y, p.z + cam.ht);

  // Maze mode: detected by the replicated MazeSpiritGrid component, which
  // only exists on the maze world's spirit cube. Gating on modelName or a
  // mesh+scale fingerprint was unsafe — KEY_SWAP_MODEL flips the avatar's
  // mesh, and overworld decoration cubes can share the spirit cube's
  // scale. This branch wins over the preview-puzzle case below because
  // MazeSpiritGrid is only ever present in the MAZE state.
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
      int slot = localOverworldPlayerSlot(game);
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

      glm::vec3 spiritPos = glm::vec3(sp.x, sp.y, sp.z + 0.6f) + side * 0.55f;
      glm::mat4 view = glm::lookAt(spiritPos, spiritPos + side, worldUp);
      return CameraState{.position = spiritPos, .view = view};
    }
  }

  // During the preview-board puzzle only; after exit, normal FPS view
  // immediately.
  if (isOverworldMazePuzzleActive(game)) {
    const glm::vec3 target(game.mazeLayout.lookAtX(), game.mazeLayout.lookAtY(),
                           game.mazeLayout.lookAtZ());
    glm::mat4 view = glm::lookAt(pos, target, worldUp);
    return CameraState{.position = pos, .view = view};
  }

  glm::quat playerRot(p.qw, p.qx, p.qy, p.qz);
  // Yaw-only so entity pitch/roll doesn't tilt the camera.
  glm::vec3 flat = playerRot * glm::vec3(0.0f, 1.0f, 0.0f);
  flat.z = 0.0f;
  flat = glm::normalize(flat);
  float yaw = std::atan2(-flat.x, flat.y);
  glm::quat yawRot = glm::angleAxis(yaw, glm::vec3(0.0f, 0.0f, 1.0f));
  glm::quat pitchRot = glm::angleAxis(cam.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
  glm::vec3 forward = yawRot * pitchRot * glm::vec3(0.0f, 1.0f, 0.0f);

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
                       const CameraState& camera, const glm::mat4& projection,
                       int quantizeLevels, bool flipZ, float softEdge) {
  glm::mat4 skyboxView = glm::mat4(glm::mat3(camera.view) * kCubemapToGame);

  glDepthFunc(GL_LEQUAL);
  shader.use();
  shader.setMat4("view", skyboxView);
  shader.setMat4("projection", projection);
  shader.setInt("skyboxQuantizeLevels", quantizeLevels);
  shader.setFloat("skyboxSoftEdge", softEdge);
  shader.setInt("skyboxFlipZ", flipZ ? 1 : 0);
  const int paletteSize = static_cast<int>(skybox.palette.size());
  shader.setInt("skyboxPaletteSize", paletteSize);
  if (paletteSize > 0) {
    shader.setVec3Array("skyboxPalette", paletteSize,
                        reinterpret_cast<const float*>(skybox.palette.data()));
  }

  glBindVertexArray(skybox.vao);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, skybox.cubemapTexture);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);
  glDepthFunc(GL_LESS);
}

static bool isTangramGhostModelName(const std::string& name) {
  return name.size() > 14 && name.starts_with("tangram_ghost_");
}

static bool isTangramPlayPieceModelName(const std::string& name) {
  return name.size() > 8 && name.starts_with("tangram_") &&
         !isTangramGhostModelName(name);
}

static bool shouldDrawTangramEntity(const ClientGame& game,
                                    const std::string& modelName) {
  if (!isOverworldTangramPuzzleActive(game)) return true;
  const uint8_t stage = tangramRoleIsolationStage(game);
  if (!shared::tangram_roles::rolesActive(stage)) return true;

  const uint8_t slot = localOverworldPlayerSlot(game);
  if (isTangramGhostModelName(modelName) &&
      !shared::tangram_roles::canSeeSlots(stage, slot)) {
    return false;
  }
  return true;
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

// Sphere-vs-frustum culler used by the per-face point-shadow loop. `planes`
// are extracted from the light-face view-projection (Gribb-Hartmann,
// normalized). A sphere is "outside" iff its signed distance to any plane is
// less than -radius.
struct ShadowCuller {
  glm::vec4 planes[6];
  bool reject(glm::vec3 c, float r) const {
    for (auto plane : planes) {
      if (glm::dot(glm::vec3(plane), c) + plane.w < -r) return true;
    }
    return false;
  }
};

static void extractFrustumPlanes(const glm::mat4& vp, glm::vec4 out[6]) {
  // glm matrices are column-major: row i = (vp[0][i], vp[1][i], vp[2][i],
  // vp[3][i]).
  glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
  glm::vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
  glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
  glm::vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
  out[0] = r3 + r0;  // left
  out[1] = r3 - r0;  // right
  out[2] = r3 + r1;  // bottom
  out[3] = r3 - r1;  // top
  out[4] = r3 + r2;  // near
  out[5] = r3 - r2;  // far
  for (int i = 0; i < 6; ++i) {
    float len = glm::length(glm::vec3(out[i]));
    if (len > 1e-6f) out[i] /= len;
  }
}

static void renderEntities(const Shader& shader, Graphics& gfx,
                           ClientGame& game,
                           std::unordered_map<std::string, Model*>& models,
                           bool forShadowPass = false,
                           const ShadowCuller* culler = nullptr) {
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
    if (!shouldDrawTangramEntity(game, renderInfo.modelName)) continue;

    // Hardcoded celestial bodies (winter moon, per-scene suns): never cast a
    // shadow (a giant silhouette would tank the scene), and only render in the
    // scene they belong to.
    std::string_view bodyScene;
    if (renderInfo.modelName == "moon")
      bodyScene = "night";
    else if (renderInfo.modelName == "sun_morning")
      bodyScene = "morning";
    else if (renderInfo.modelName == "sun_sunset")
      bodyScene = "sunset";
    if (!bodyScene.empty()) {
      if (forShadowPass) continue;
      auto* sc = currentScene(game);
      if (!sc || sc->name != bodyScene) continue;
    }

    std::string modelKey = renderInfo.modelName;
    if (isTangramGhostModelName(renderInfo.modelName)) {
      modelKey = renderInfo.modelName + "_colored";
    } else if (isTangramPlayPieceModelName(renderInfo.modelName)) {
      const uint8_t stage = tangramRoleIsolationStage(game);
      const uint8_t slot = localOverworldPlayerSlot(game);
      if (shared::tangram_roles::colorRestricted(stage) &&
          !shared::tangram_roles::canSeeColor(stage, slot)) {
        modelKey = renderInfo.modelName + "_mute";
      }
    }
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
    glm::vec3 scale(renderInfo.sx, renderInfo.sy, renderInfo.sz);
    glm::vec3 pos(p.x, p.y, p.z);

    // Fragments: rainbow-tinted, bob up and down (Z is up), and exempt from the
    // color-restoration recolor so they always read in full color. The suns are
    // also exempt (a desaturated sun looks broken). Everything else (incl. the
    // terrain) desaturates outside the player's box as normal. These per-draw
    // uniforms are no-ops in the shadow pass (no such uniforms).
    const bool isFragment = renderInfo.modelName == "fragment";
    const bool isSun = renderInfo.modelName.starts_with("sun_");
    shader.setInt("alwaysColor", (isFragment || isSun) ? 1 : 0);
    shader.setFloat("rainbowStrength", isFragment ? 1.0f : 0.0f);
    if (isFragment) {
      const auto t = static_cast<float>(glfwGetTime());
      shader.setFloat("rainbowTime", t);
      // Phase by entity id so multiple fragments don't bob in lockstep.
      pos.z +=
          0.25f * std::sin(t * 2.0f + static_cast<float>(entity.id) * 0.7f);
    }

    // Cheap sphere reject before composing the model matrix or touching GL.
    if (culler && modelAsset->localBoundsRadius > 0.0f) {
      glm::vec3 localCenter =
          modelAsset->orientation * modelAsset->localBoundsCenter;
      glm::vec3 worldCenter = pos + rotation * (scale * localCenter);
      float maxScale =
          std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
      float worldRadius = modelAsset->localBoundsRadius * maxScale;
      if (culler->reject(worldCenter, worldRadius)) continue;
    }

    auto model = glm::identity<glm::mat4>();
    model = glm::translate(model, pos);
    model = glm::scale(model, scale);
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

  // Kick off a worker that parses the landscape glTF (the dominant single
  // CPU cost in load()). All the small asset loads + shader compilation +
  // FBO allocs run in parallel on the main thread; when we reach the map
  // upload stage below, we just await the parsed scene and skip the parse.
  std::future<std::shared_ptr<shared::ParsedModel>> landscapeParse =
      std::async(std::launch::async, [] {
        auto parsed = std::make_shared<shared::ParsedModel>();
        const std::string fullPath =
            (exeDir() / shared::DEFAULT_MAP_PATH).string();
        if (!parsed->load(fullPath, shared::MAP_LOAD_FLAGS)) {
          std::cout << "ERROR::ASSIMP::landscapeParse: " << parsed->lastError()
                    << '\n';
        }
        return parsed;
      });

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
  videoYuvShader.emplace("shaders/vertex_present.glsl",
                         "shaders/fragment_video_yuv.glsl");
  videoQuadShader.emplace("shaders/vertex_video_quad.glsl",
                          "shaders/fragment_video_yuv.glsl");

  // Shader ctor failures are silent at runtime so F5 hot-reload can keep
  // the previous program — at startup there is no previous program, so
  // fail fast instead of starting with a black window.
  const std::optional<Shader>* required[] = {
      &gbufferShader, &lightingShader, &lightingCelShader, &outlineSobelShader,
      &skyboxShader,  &presentShader,  &blurShader,        &tonemapShader,
      &ssaoShader,    &ssaoBlurShader, &shadowDirShader,   &shadowPointShader,
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

  // Unit quad (XY plane, z=0) for the in-world video screen. pos@0, uv@2 to
  // match vertex_video_quad.glsl.
  {
    const float quadVerts[] = {
        -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.5f,  -0.5f, 0.0f, 1.0f, 0.0f,
        0.5f,  0.5f,  0.0f, 1.0f, 1.0f, -0.5f, 0.5f,  0.0f, 0.0f, 1.0f,
    };
    const unsigned int quadIdx[] = {0, 1, 2, 0, 2, 3};
    glGenVertexArrays(1, &videoQuadVAO);
    glGenBuffers(1, &videoQuadVBO);
    glGenBuffers(1, &videoQuadEBO);
    glBindVertexArray(videoQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, videoQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, videoQuadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIdx), quadIdx,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
  }

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
    // The moon and the per-scene suns are procedural spheres; everything else
    // with a CubeSpec is a cube. Mesh-backed assets go through loadModel as
    // before. The 10× emissive boost pushes these bodies well past the bloom
    // threshold so they glow hard on the skybox.
    bool celestialSphere =
        asset.name == "moon" || asset.name.starts_with("sun_");
    Model* m =
        asset.cubeSpec
            ? (celestialSphere ? makeSphereModel(*asset.cubeSpec, 16, 28, 10.0f)
                               : makeCubeModel(*asset.cubeSpec))
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

  for (const auto& def : shared::tangram_puzzle::kPieces) {
    Model* m = makeTangramPieceModel(def);
    if (m) {
      m->orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
      models[std::string(def.modelName)] = m;
      printf("Loaded tangram mesh: %s\n", def.modelName);
    }
    Model* mute = makeTangramPieceMuteModel(def);
    if (mute) {
      mute->orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
      models[std::string(def.modelName) + "_mute"] = mute;
    }
    const std::string ghostName =
        std::string(shared::tangram_puzzle::ghostModelForId(def.id));
    Model* ghost = makeTangramGhostSlotModel(def);
    if (ghost) {
      ghost->orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
      models[ghostName] = ghost;
    }
    Model* ghostColored = makeTangramColoredGhostSlotModel(def);
    if (ghostColored) {
      ghostColored->orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
      models[ghostName + "_colored"] = ghostColored;
      printf("Loaded tangram ghost: %s (colored slot guide)\n",
             ghostName.c_str());
    }
  }

  // Per-node sub-models keyed to match RenderInfo.modelName from map_loader.
  // landscapeParse was launched at the top of load(); await it here. If the
  // worker already finished, .get() is non-blocking. The GL-upload step then
  // runs on the main thread (VAOs/VBOs require the main GL context).
  renderLoadingFrame(std::string("Waiting on landscape parse: ") +
                     std::string(shared::DEFAULT_MAP_PATH));
  auto pump = [this] { pumpLoadingFrame(); };
  auto parsedLandscape = landscapeParse.get();
  renderLoadingFrame(std::string("Loading map: ") +
                     std::string(shared::DEFAULT_MAP_PATH));
  auto mapModels = loadMapModels(*parsedLandscape, pump);
  for (auto& [key, m] : mapModels) {
    models[key] = m;
    printf("Loaded map sub-model: %s\n", key.c_str());
    pumpLoadingFrame();
  }

  for (const auto& sc : shared::SCENES) {
    std::string dir = std::string(sc.skyboxDirectory);
    if (skyboxes.find(dir) == skyboxes.end()) {
      renderLoadingFrame(std::string("Loading skybox: ") + dir);
      skyboxes[dir] = loadSkybox(dir, pump);
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
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, rw, rh, 0, fmt, type, nullptr);
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, rw, rh, 0, GL_RGBA, GL_FLOAT,
                 nullptr);
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

  const int ssaoScale = std::max(1, settings.ssaoScale);
  ssaoWidth = std::max(1, rw / ssaoScale);
  ssaoHeight = std::max(1, rh / ssaoScale);
  lastSsaoScale = ssaoScale;
  // Bilinear when the lighting pass needs to upscale; NEAREST otherwise so
  // single-pixel features stay crisp.
  const GLint ssaoFilter = ssaoScale > 1 ? GL_LINEAR : GL_NEAREST;

  auto allocSsao = [&](GLuint& fbo, GLuint& tex) {
    if (!fbo) glGenFramebuffers(1, &fbo);
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, ssaoWidth, ssaoHeight, 0, GL_RED,
                 GL_FLOAT, nullptr);
    GPU_MEM_TEX2D("SSAO", GL_R16F, ssaoWidth, ssaoHeight);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ssaoFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ssaoFilter);
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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               nullptr);
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
                  &outlineSobelShader, &ssaoShader, &videoQuadShader}) {
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
      {.slot = videoYuvShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_video_yuv.glsl",
       .geom = ""},
      {.slot = videoQuadShader,
       .vert = "shaders/vertex_video_quad.glsl",
       .frag = "shaders/fragment_video_yuv.glsl",
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

  // F8: play the first clip fullscreen locally (no server needed) for testing.
  bool f8 = glfwGetKey(window, GLFW_KEY_F8) == GLFW_PRESS;
  if (f8 && !keyF8Prev) handleVideoRequest(VideoRequest{0, 0, 0, 0, false});
  keyF8Prev = f8;

  // Enter dismisses an active fullscreen cutscene.
  bool skip = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
  if (skip && !keySkipPrev && videoMode == VideoMode::Fullscreen) {
    if (videoPlayer) videoPlayer->stop();
    videoMode = VideoMode::None;
  }
  keySkipPrev = skip;
}

void Graphics::handleVideoRequest(const VideoRequest& req) {
  if (req.stop) {
    if (videoPlayer) videoPlayer->stop();
    videoMode = VideoMode::None;
    return;
  }
  const std::string path = videoPathFor(req.videoId);
  if (path.empty()) {
    fprintf(stderr, "handleVideoRequest: unknown videoId %u\n", req.videoId);
    return;
  }
  videoPlayer.emplace();  // destroys any previous player (frees its GL textures)
  if (!videoPlayer->open(path, req.loop != 0)) {
    videoPlayer.reset();
    videoMode = VideoMode::None;
    return;
  }
  videoMode = (req.mode == 1) ? VideoMode::InWorld : VideoMode::Fullscreen;
  videoTargetEntityId = req.targetEntityId;
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

static void drawTangramCrosshair(int fbWidth, int fbHeight) {
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_SCISSOR_TEST);
  const int cx = fbWidth / 2;
  const int cy = fbHeight / 2;
  glClearColor(1.0f, 1.0f, 1.0f, 0.9f);
  const int r = 2;
  glScissor(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glClearColor(1.0f, 1.0f, 1.0f, 0.65f);
  const int arm = 7;
  const int thick = 1;
  glScissor(cx - arm, cy - thick, 2 * arm + 1, 2 * thick + 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glScissor(cx - thick, cy - arm, 2 * thick + 1, 2 * arm + 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
}

void Graphics::render(ClientGame& game, ClientNetwork& network) {
  SIMPLE_PROFILE_SCOPE("Render");
  GPU_PROFILE_SCOPE("Render");

  if (game.serverLost.load(std::memory_order_acquire)) {
    renderLostConnectionScreen(game);
    return;
  }

  if (game.currentGameState == shared::GameStateType::CREDITS) {
    renderCreditsScreen(game);
    return;
  }

  auto camera = computeCamera(game);
  if (!camera) return;

  game.tangramCrosshairTargetId =
      isOverworldTangramPuzzleActive(game)
          ? pickTangramPieceAtScreenCenter(game, camera->view, projection)
          : 0;

  // Per-skybox quantize overrides: when the active scene changes, push any
  // non-sentinel values from SceneInfo onto the live GraphicsSettings. The
  // palette-rebuild block below picks the new value up the same frame.
  if (const auto* sc = currentScene(game); sc != lastAppliedScene) {
    if (sc) {
      if (sc->skyboxQuantizeLevels >= 0)
        settings.skyboxQuantizeLevels = sc->skyboxQuantizeLevels;
      if (sc->skyboxPaletteColors >= 0)
        settings.skyboxPaletteColors = sc->skyboxPaletteColors;
    }
    lastAppliedScene = sc;
  }
  if (settings.dirShadowMapSize != lastDirShadowSize) {
    allocateDirShadowMap(settings.dirShadowMapSize);
    lastDirShadowSize = settings.dirShadowMapSize;
  }
  if (settings.pointShadowMapSize != lastPointShadowSize) {
    allocatePointShadowMaps(settings.pointShadowMapSize);
    lastPointShadowSize = settings.pointShadowMapSize;
  }
  if (std::max(1, settings.pixelationScale) != lastPixelationScale ||
      std::max(1, settings.ssaoScale) != lastSsaoScale) {
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
  if (settings.skyboxPaletteColors != lastSkyboxPaletteColors) {
    const int colors =
        std::min(settings.skyboxPaletteColors, shared::kMaxPaletteColors);
    for (auto& [dir, sb] : skyboxes) {
      buildSkyboxPalette(sb, colors);
    }
    lastSkyboxPaletteColors = settings.skyboxPaletteColors;
  }
  if (settings.shadowsEnabled != prevShadowsEnabled) {
    if (!settings.shadowsEnabled) clearShadowMaps();
    prevShadowsEnabled = settings.shadowsEnabled;
  }

  // Advance per-entity animators using real wallclock dt (independent of the
  // server tick). Only animated, skinned entities pay any work here.
  const double now = glfwGetTime();
  const float dt =
      lastFrameTime == 0.0 ? 0.0f : static_cast<float>(now - lastFrameTime);
  lastFrameTime = now;
  if (videoPlayer && videoPlayer->isPlaying()) {
    videoPlayer->update(dt);
    // A finished non-looping cutscene clears itself.
    if (!videoPlayer->isPlaying() && videoMode == VideoMode::Fullscreen) {
      videoMode = VideoMode::None;
    }
  }
  updateAnimators(*this, game, dt);

  projection = glm::perspective(
      glm::radians(settings.fovDegrees),
      static_cast<float>(fbWidth) / static_cast<float>(fbHeight),
      settings.nearPlane, settings.farPlane);

  // Collect lights up front so shadow passes and the lighting pass agree
  // on shadow-slot assignments.
  LightUpload lights[kMaxLightingShaderLights];
  int numLights = collectPointLights(game, lights);
  if (!settings.pointShadowsEnabled) {
    for (int i = 0; i < numLights; ++i) lights[i].shadowIdx = -1;
  }

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
  if (settings.shadowsEnabled && settings.pointShadowsEnabled &&
      shadowPointShader && shadowPointShader->valid() && numLights > 0) {
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
        const glm::mat4& vp = pointMats[slot * 6 + f];
        shadowPointShader->setMat4("lightSpaceMatrix", vp);
        ShadowCuller culler;
        extractFrustumPlanes(vp, culler.planes);
        renderEntities(*shadowPointShader, *this, game, models,
                       /*forShadowPass=*/true, &culler);
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
    glViewport(0, 0, ssaoWidth, ssaoHeight);
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
    // Noise tile size is in the SSAO render's own pixel space.
    ssaoShader->setVec2("noiseScale", static_cast<float>(ssaoWidth) / 4.0f,
                        static_cast<float>(ssaoHeight) / 4.0f);
    glBindVertexArray(fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
  } else if (!settings.ssaoEnabled) {
    // Clear blurred SSAO to 1.0 so the lighting pass reads "no occlusion".
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
    glViewport(0, 0, ssaoWidth, ssaoHeight);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // Restore so a future pass that forgets to set its own clearColor before
    // glClear doesn't inherit white.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  }

  if (settings.ssaoEnabled && ssaoBlurShader && ssaoBlurShader->valid()) {
    SIMPLE_PROFILE_SCOPE("SSAOBlur");
    GPU_PROFILE_SCOPE("SSAOBlur");
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
    glViewport(0, 0, ssaoWidth, ssaoHeight);
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
      lighting->setFloat("bloomThreshold", settings.bloomEnabled
                                               ? settings.bloomThreshold
                                               : 1e9f);
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
        // Gate on the path being non-empty: ensureCelRampLoaded() always
        // allocates a 1x1 white fallback, so a non-zero celRampTexture is not
        // sufficient — sampling white would force diffuse factor to 1.0.
        const bool rampOk = settings.celUseRampTexture &&
                            !settings.celRampPath.empty() && celRampTexture;
        lighting->setInt("useRampTexture", rampOk ? 1 : 0);
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
    glBlitFramebuffer(0, 0, renderWidth, renderHeight, 0, 0, renderWidth,
                      renderHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
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
        drawSkybox(*skyboxShader, it->second, *camera, projection,
                   settings.skyboxQuantizeLevels, sceneInfo->skyboxFlipZ,
                   settings.skyboxSoftEdge);
      }
    }
  }

  // In-world video screen: forward draw into the HDR litFBO (still bound; the
  // skybox pass blitted g-buffer depth into litDepth and left depth-test on).
  // Emissive, depth-tested, written to litColor only (COLOR_ATTACHMENT0).
  if (videoMode == VideoMode::InWorld && videoPlayer &&
      videoPlayer->isPlaying() && videoQuadShader && videoQuadShader->valid() &&
      videoQuadVAO) {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // Screen transform: from the target entity's Position if present, else a
    // fixed quad. Orientation/scale likely need tuning per real placement.
    float h = 3.0f;
    float w = h * videoPlayer->aspect();
    glm::vec3 center(0.0f, 5.0f, 0.0f);
    auto it = game.renderEntityMap.find(videoTargetEntityId);
    if (videoTargetEntityId != 0 && it != game.renderEntityMap.end() &&
        game.renderRegistry.valid(it->second) &&
        game.renderRegistry.all_of<shared::Position>(it->second)) {
      const auto& p = game.renderRegistry.get<shared::Position>(it->second);
      center = glm::vec3(p.x, p.y, p.z);
    }
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(w, h, 1.0f));

    videoQuadShader->use();
    videoQuadShader->setMat4("model", model);
    videoPlayer->bindPlanes(0, 1, 2);
    videoQuadShader->setInt("texY", 0);
    videoQuadShader->setInt("texCb", 1);
    videoQuadShader->setInt("texCr", 2);
    videoQuadShader->setVec2("texScale", videoPlayer->texScaleX(),
                             videoPlayer->texScaleY());
    videoQuadShader->setVec2("fit", 1.0f, 1.0f);
    videoQuadShader->setInt("linearize", 1);
    videoQuadShader->setFloat("emissiveBoost", 1.5f);
    glBindVertexArray(videoQuadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
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
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, gAlbedo);
      tonemapShader->setInt("gAlbedo", 3);
      tonemapShader->setFloat("exposure", settings.exposure);
      tonemapShader->setFloat("bloomStrength", settings.bloomEnabled
                                                   ? settings.bloomStrength
                                                   : 0.0f);

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
          const auto& b =
              game.renderRegistry.get<shared::ColorBoundingBox>(selfIt->second);
          restoreMin = glm::vec3(b.minX, b.minY, b.minZ);
          restoreMax = glm::vec3(b.maxX, b.maxY, b.maxZ);
          restorationStrength = settings.colorRestorationStrength;
        }
      }
      tonemapShader->setFloat("colorRestorationStrength", restorationStrength);
      tonemapShader->setFloat("colorRestorationEdgeWidth",
                              settings.colorRestorationEdgeWidth);
      tonemapShader->setFloat("colorRestorationLightStrength",
                              settings.colorRestorationLightStrength);
      tonemapShader->setVec3("colorRestorationMin", restoreMin);
      tonemapShader->setVec3("colorRestorationMax", restoreMax);

      const shared::tangram::ColorRestoreAabb tangramColor =
          game.tangramArena.alwaysColorAabb();
      tonemapShader->setFloat("tangramAlwaysColorEnabled", 1.0f);
      tonemapShader->setVec3(
          "tangramAlwaysColorMin",
          glm::vec3(tangramColor.minX, tangramColor.minY, tangramColor.minZ));
      tonemapShader->setVec3(
          "tangramAlwaysColorMax",
          glm::vec3(tangramColor.maxX, tangramColor.maxY, tangramColor.maxZ));

      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
    }
  }

  GLuint finalLDR = ldrColor;
  if (settings.outlineMode == OutlineMode::Sobel && outlineSobelShader &&
      outlineSobelShader->valid()) {
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
    outlineSobelShader->setFloat("outlineSobelWidth",
                                 settings.outlineSobelWidth);
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

  if (isOverworldTangramPuzzleActive(game)) {
    const uint8_t stage = tangramRoleIsolationStage(game);
    const uint8_t slot = localOverworldPlayerSlot(game);
    if (shared::tangram_roles::canRotate(stage, slot)) {
      drawTangramCrosshair(fbWidth, fbHeight);
    }
  }

  // Fullscreen video overlay: drawn over the finished (post-tonemap) frame, so
  // it outputs display-referred RGB directly. Game keeps running underneath;
  // Enter dismisses (see processDebugKeys).
  if (videoMode == VideoMode::Fullscreen && videoPlayer &&
      videoPlayer->isPlaying() && videoYuvShader && videoYuvShader->valid() &&
      fullscreenVAO) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbWidth, fbHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    videoYuvShader->use();
    videoPlayer->bindPlanes(0, 1, 2);
    videoYuvShader->setInt("texY", 0);
    videoYuvShader->setInt("texCb", 1);
    videoYuvShader->setInt("texCr", 2);
    videoYuvShader->setVec2("texScale", videoPlayer->texScaleX(),
                            videoPlayer->texScaleY());
    const float winAspect =
        fbHeight > 0 ? static_cast<float>(fbWidth) / fbHeight : 1.0f;
    const float vidAspect = videoPlayer->aspect();
    const glm::vec2 fit = (winAspect > vidAspect)
                              ? glm::vec2(winAspect / vidAspect, 1.0f)
                              : glm::vec2(1.0f, vidAspect / winAspect);
    videoYuvShader->setVec2("fit", fit.x, fit.y);
    videoYuvShader->setInt("linearize", 0);
    videoYuvShader->setFloat("emissiveBoost", 1.0f);
    glBindVertexArray(fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
  }

  drawDebugOverlay();

  drawSettingsUIFrame(game);
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
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_FUNC,
                  GL_LEQUAL);
  glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
  // Bind layer 0 first so the FBO is complete; the render loop rebinds each
  // layer as it draws.
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            pointShadowMaps, 0, 0);
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
    unsigned char* pixels = stbi_load(path.string().c_str(), &w, &h, &ch, 3);
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
  for (const auto& face : faces) {
    const auto base = static_cast<GLuint>(verts.size() / 6);
    for (auto corner : face.corners) {
      verts.push_back(corner.x);
      verts.push_back(corner.y);
      verts.push_back(corner.z);
      verts.push_back(face.normal.x);
      verts.push_back(face.normal.y);
      verts.push_back(face.normal.z);
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
               static_cast<GLsizeiptr>(idx.size() * sizeof(GLuint)), idx.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void*)nullptr);
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
  // Stage label changed (or this is the first call): always render so the
  // user sees the new text immediately, regardless of pacing.
  const bool stageChanged = status != loadingStatus;
  loadingStatus = status;
  if (!window || !loadingShader || !loadingShader->valid() ||
      !loadingCubeIndexCount) {
    return;
  }

  // Skip when we already drew a frame within the last 1/60 s. Loaders can
  // call this freely (per-node, per-face) without paying a draw + swap each
  // time; the first call past the deadline renders, the rest are cheap no-ops.
  // This is what gets the loading screen to actual 60 fps inside long
  // single-stage loads instead of freezing between named stages.
  constexpr double kInterval = 1.0 / kLoadingTargetFps;
  const double now = glfwGetTime();
  if (!stageChanged && loadingLastFrameTime > 0.0 &&
      now < loadingLastFrameTime + kInterval) {
    return;
  }
  loadingLastFrameTime = now;

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

  const float aspect =
      static_cast<float>(fbWidth) / static_cast<float>(std::max(1, fbHeight));
  const auto t = static_cast<float>(glfwGetTime() - loadingStartTime);
  glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
  glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
  glm::mat4 model = glm::rotate(glm::mat4(1.0f), t * 1.2f,
                                glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
  glm::mat4 mvp = proj * view * model;
  glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

  loadingShader->use();
  loadingShader->setMat4("mvp", mvp);
  loadingShader->setMat3("normalMatrix", normalMatrix);
  glBindVertexArray(loadingCubeVAO);
  glDrawElements(GL_TRIANGLES, loadingCubeIndexCount, GL_UNSIGNED_INT, nullptr);
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
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##LoadingStatus", nullptr, flags)) {
      ImGui::SetWindowFontScale(2.0f);
      ImGui::TextUnformatted("Loading...");
      ImGui::Separator();
      ImGui::TextUnformatted(loadingStatus.c_str());
    }
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  glfwSwapBuffers(window);
}

void Graphics::pumpLoadingFrame() {
  // Reuse the named-stage path with the cached status — the early-exit pacing
  // inside renderLoadingFrame keeps repeated calls cheap.
  renderLoadingFrame(loadingStatus);
}

void Graphics::renderCreditsScreen(ClientGame& game) {
  (void)game;
  if (!window) return;
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
  if (fbWidth <= 0 || fbHeight <= 0) return;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbWidth, fbHeight);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!ImGui::GetCurrentContext()) return;

  const double now = glfwGetTime();
  if (creditsStartTime < 0.0) creditsStartTime = now;
  const float elapsed = static_cast<float>(now - creditsStartTime);

  // Edit these lines to credit the team.
  static const char* kCreditsLines[] = {
      "Thanks for playing",
      "",
      "",
      "A CSE 125 Production",
      "",
      "Programming",
      "The Team",
      "",
      "Art & World",
      "The Team",
      "",
      "Audio",
      "The Team",
      "",
      "",
      "See you next season",
  };

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  ImGuiIO& io = ImGui::GetIO();

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##Credits", nullptr, flags)) {
    ImGui::SetWindowFontScale(2.4f);
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    const float scrollSpeed = 70.0f;  // pixels per second, bottom -> top
    float y = io.DisplaySize.y - elapsed * scrollSpeed;
    for (const char* line : kCreditsLines) {
      if (line[0] != '\0') {
        const float textW = ImGui::CalcTextSize(line).x;
        ImGui::SetCursorPos(
            ImVec2((io.DisplaySize.x - textW) * 0.5f, y));
        ImGui::TextUnformatted(line);
      }
      y += lineH;
    }

    // Dismiss hint pinned at the bottom (does not scroll).
    ImGui::SetWindowFontScale(1.2f);
    const char* hint = "Press Enter to return";
    const float hintW = ImGui::CalcTextSize(hint).x;
    ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - hintW) * 0.5f,
                               io.DisplaySize.y - 40.0f));
    ImGui::TextUnformatted(hint);
  }
  ImGui::End();
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  // No buffer swap here — the main loop calls graphics.swap() after render().
}

void Graphics::renderLostConnectionScreen(ClientGame& game) {
  (void)game;
  if (!window) return;
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
  if (fbWidth <= 0 || fbHeight <= 0) return;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbWidth, fbHeight);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!ImGui::GetCurrentContext()) return;

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  ImGuiIO& io = ImGui::GetIO();

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##LostConnection", nullptr, flags)) {
    ImGui::SetWindowFontScale(2.4f);
    const char* title = "Lost connection to server";
    const float titleW = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - titleW) * 0.5f,
                               io.DisplaySize.y * 0.5f - 40.0f));
    ImGui::TextUnformatted(title);

    ImGui::SetWindowFontScale(1.2f);
    const char* hint = "Close the window to exit, then relaunch to reconnect";
    const float hintW = ImGui::CalcTextSize(hint).x;
    ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - hintW) * 0.5f,
                               io.DisplaySize.y * 0.5f + 20.0f));
    ImGui::TextUnformatted(hint);
  }
  ImGui::End();
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  // No buffer swap here — the main loop calls graphics.swap() after render().
}

Graphics::ServerMenuResult Graphics::renderServerMenuFrame(
    char* host, size_t hostSize, int* port, const char* statusMsg) {
  ServerMenuResult result = ServerMenuResult::None;
  if (!window || !ImGui::GetCurrentContext()) return ServerMenuResult::Quit;

  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
  if (fbWidth <= 0 || fbHeight <= 0) return ServerMenuResult::None;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbWidth, fbHeight);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
      ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Always);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoCollapse;
  if (ImGui::Begin("Connect to Server", nullptr, flags)) {
    bool submit = false;

    ImGui::TextUnformatted("Server IP / hostname");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##host", host, hostSize,
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
      submit = true;
    }

    ImGui::TextUnformatted("Port");
    ImGui::SetNextItemWidth(-FLT_MIN);
    // InputScalar (and so InputInt) asserts that EnterReturnsTrue is not set;
    // detect Enter manually by watching for an Enter-press on the active item.
    ImGui::InputInt("##port", port, 0, 0);
    if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      submit = true;
    }
    if (*port < 1) *port = 1;
    if (*port > 65535) *port = 65535;

    if (statusMsg && statusMsg[0] != '\0') {
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.45f, 1.0f));
      ImGui::TextWrapped("%s", statusMsg);
      ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    if (ImGui::Button("Connect", ImVec2(160.0f, 0.0f))) submit = true;
    ImGui::SameLine();
    if (ImGui::Button("Quit", ImVec2(160.0f, 0.0f))) {
      result = ServerMenuResult::Quit;
    }

    if (submit) result = ServerMenuResult::Connect;
  }
  ImGui::End();

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  glfwSwapBuffers(window);
  return result;
}

void Graphics::shutdownImGui() {
  if (!ImGui::GetCurrentContext()) return;
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

static void drawFPSOverlay() {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.45f);
  if (ImGui::Begin("##fps", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                       ImGuiWindowFlags_NoFocusOnAppearing |
                       ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::Text("%.0f FPS (%.2f ms)", io.Framerate,
                io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
  }
  ImGui::End();
}

void Graphics::drawSettingsUIFrame(ClientGame& game) {
  if (!ImGui::GetCurrentContext()) return;
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  drawPuzzleHUDs(game);
  if (settings.showFPS) drawFPSOverlay();
  if (settingsMenuOpen) drawSettingsUI(settings, settingsMenuOpen);
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
