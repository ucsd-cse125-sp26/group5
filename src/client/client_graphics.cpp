#include "client_graphics.h"

#include <cmath>
#include <string>

#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shared/assets.h"
#include "shared/components.h"

// Skybox images use Y-up; the game uses Z-up.
// cubemap->game: X->X, Y->Z, Z->-Y  (column-major)
static const glm::mat3 kCubemapToGame(1, 0, 0, 0, 0, 1, 0, -1, 0);

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

void setupCameraMatrix(GLuint shaderProgram, const CameraState& camera) {
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE,
                     glm::value_ptr(camera.view));
  glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), camera.position.x,
              camera.position.y, camera.position.z);
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

// clang-format off
static const float skyboxVertices[] = {
    -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,

     1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,

    -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f,
};
// clang-format on

GLuint initSkyboxVAO() {
  GLuint vao, vbo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
  glBindVertexArray(0);
  return vao;
}

void drawSkybox(GLuint skyboxShader, GLuint skyboxVAO, GLuint cubemapTexture,
                const CameraState& camera, const glm::mat4& projection) {
  glm::mat4 skyboxView = glm::mat4(glm::mat3(camera.view) * kCubemapToGame);

  glDepthFunc(GL_LEQUAL);
  glUseProgram(skyboxShader);
  glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "view"), 1, GL_FALSE,
                     glm::value_ptr(skyboxView));
  glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "projection"), 1,
                     GL_FALSE, glm::value_ptr(projection));

  glBindVertexArray(skyboxVAO);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);
  glDepthFunc(GL_LESS);
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
