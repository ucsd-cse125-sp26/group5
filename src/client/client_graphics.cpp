#include "client_graphics.h"

#include <string>

#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shared/assets.h"
#include "shared/components.h"

void initPointLights(GLuint shaderProgram) {
  for (int pl = 0; pl < 4; pl++) {
    std::string prefix = "pointLights[" + std::to_string(pl) + "].";
    glUniform1f(
        glGetUniformLocation(shaderProgram, (prefix + "constant").c_str()),
        1.0f);
    glUniform1f(
        glGetUniformLocation(shaderProgram, (prefix + "linear").c_str()), 0.0f);
    glUniform1f(
        glGetUniformLocation(shaderProgram, (prefix + "quadratic").c_str()),
        0.0f);
    glUniform3f(
        glGetUniformLocation(shaderProgram, (prefix + "ambient").c_str()), 0.0f,
        0.0f, 0.0f);
    glUniform3f(
        glGetUniformLocation(shaderProgram, (prefix + "diffuse").c_str()), 0.0f,
        0.0f, 0.0f);
    glUniform3f(
        glGetUniformLocation(shaderProgram, (prefix + "specular").c_str()),
        0.0f, 0.0f, 0.0f);
  }
}

bool setupCameraMatrix(GLuint shaderProgram, const ClientGame& game) {
  glm::vec3 cameraPos(0.0f, 0.0f, 10.0f);
  glm::vec3 cameraTarget(0.0f, 0.0f, 0.0f);
  const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);

  auto selfIt = game.entityMap.find(game.myEntityId);
  if (selfIt != game.entityMap.end() && game.registry.valid(selfIt->second) &&
      game.registry.all_of<shared::Position, shared::Camera>(selfIt->second)) {
    const auto& p = game.registry.get<shared::Position>(selfIt->second);
    const auto& cam = game.registry.get<shared::Camera>(selfIt->second);

    glm::quat playerRot(p.qw, p.qx, p.qy, p.qz);
    glm::quat pitchRot = glm::angleAxis(cam.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 forward = playerRot * pitchRot * glm::vec3(0.0f, 1.0f, 0.0f);

    cameraPos = glm::vec3(p.x, p.y, p.z + cam.ht);
    cameraTarget = cameraPos + forward;
  } else {
    return false;
  }

  glm::mat4 viewMat = glm::lookAt(cameraPos, cameraTarget, worldUp);

  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE,
                     glm::value_ptr(viewMat));
  glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), cameraPos.x,
              cameraPos.y, cameraPos.z);
  return true;
}

void updateDirectionalLight(GLuint shaderProgram, const ClientGame& game) {
  auto dlView = game.registry.view<shared::DirectionalLight>();
  for (auto ent : dlView) {
    auto& dl = dlView.get<shared::DirectionalLight>(ent);
    glUniform3f(glGetUniformLocation(shaderProgram, "dirLight.direction"),
                dl.dirX, dl.dirY, dl.dirZ);
    glUniform3f(glGetUniformLocation(shaderProgram, "dirLight.ambient"),
                dl.ambientR, dl.ambientG, dl.ambientB);
    glUniform3f(glGetUniformLocation(shaderProgram, "dirLight.diffuse"),
                dl.diffuseR, dl.diffuseG, dl.diffuseB);
    glUniform3f(glGetUniformLocation(shaderProgram, "dirLight.specular"),
                dl.specularR, dl.specularG, dl.specularB);
    break;
  }
}

void updatePointLights(GLuint shaderProgram, const ClientGame& game) {
  int plIdx = 0;
  auto lightView = game.registry.view<shared::PointLight>();
  for (auto ent : lightView) {
    if (plIdx >= 4) break;
    auto& pl = lightView.get<shared::PointLight>(ent);
    std::string prefix = "pointLights[" + std::to_string(plIdx) + "].";
    glUniform3f(
        glGetUniformLocation(shaderProgram, (prefix + "position").c_str()),
        pl.px, pl.py, pl.pz);
    glUniform1f(
        glGetUniformLocation(shaderProgram, (prefix + "constant").c_str()),
        pl.constant);
    glUniform1f(
        glGetUniformLocation(shaderProgram, (prefix + "linear").c_str()),
        pl.linear);
    glUniform1f(
        glGetUniformLocation(shaderProgram, (prefix + "quadratic").c_str()),
        pl.quadratic);
    glUniform3f(
        glGetUniformLocation(shaderProgram, (prefix + "ambient").c_str()),
        pl.ambientR, pl.ambientG, pl.ambientB);
    glUniform3f(
        glGetUniformLocation(shaderProgram, (prefix + "diffuse").c_str()),
        pl.diffuseR, pl.diffuseG, pl.diffuseB);
    glUniform3f(
        glGetUniformLocation(shaderProgram, (prefix + "specular").c_str()),
        pl.specularR, pl.specularG, pl.specularB);
    plIdx++;
  }
}

void renderEntities(GLuint shaderProgram, ClientGame& game,
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

    Draw(shaderProgram, *modelAsset, model);
  }
}
