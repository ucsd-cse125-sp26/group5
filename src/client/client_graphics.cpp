#include "client_graphics.h"

#include <cmath>
#include <string>

#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "shaders.h"
#include "shared/assets.h"
#include "shared/components.h"

// Skybox images use Y-up; the game uses Z-up.
// cubemap->game: X->X, Y->Z, Z->-Y  (column-major)
static const glm::mat3 kCubemapToGame(1, 0, 0, 0, 0, 1, 0, -1, 0);

void initPointLights(const Shader& shader) {
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

void setupCameraMatrix(const Shader& shader, const CameraState& camera) {
  shader.setMat4("view", camera.view);
  shader.setVec3("viewPos", camera.position.x, camera.position.y,
                 camera.position.z);
}

void updateDirectionalLight(const Shader& shader, const ClientGame& game) {
  auto dlView = game.registry.view<shared::DirectionalLight>();
  for (auto ent : dlView) {
    auto& dl = dlView.get<shared::DirectionalLight>(ent);
    shader.setVec3("dirLight.direction", dl.dirX, dl.dirY, dl.dirZ);
    shader.setVec3("dirLight.ambient", dl.ambientR, dl.ambientG, dl.ambientB);
    shader.setVec3("dirLight.diffuse", dl.diffuseR, dl.diffuseG, dl.diffuseB);
    shader.setVec3("dirLight.specular", dl.specularR, dl.specularG,
                   dl.specularB);
    break;
  }
}

void updatePointLights(const Shader& shader, const ClientGame& game) {
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

void drawSkybox(const Shader& shader, const Skybox& skybox,
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

void renderEntities(const Shader& shader, ClientGame& game,
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
    glm::quat modelOrient(1.0f, 0.0f, 0.0f, 0.0f);
    if (const auto* info = shared::findAsset(renderInfo.modelName)) {
      modelOrient = glm::quat(info->qw, info->qx, info->qy, info->qz);
    }
    auto model = glm::identity<glm::mat4>();
    model = glm::translate(model, glm::vec3(p.x, p.y, p.z));
    model = glm::scale(model, glm::vec3(renderInfo.scale));
    model = model * glm::mat4_cast(rotation) * glm::mat4_cast(modelOrient);

    Draw(shader, *modelAsset, model);
  }
}
