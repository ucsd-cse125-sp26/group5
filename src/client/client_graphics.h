#pragma once

#include <optional>
#include <unordered_map>

#include "asset.h"
#include "client/client_game.h"
#include "glad/gl.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"

struct CameraState {
  glm::vec3 position;
  glm::mat4 view;
};

std::optional<CameraState> computeCamera(const ClientGame& game);
void initPointLights(GLuint shaderProgram);
void setupCameraMatrix(GLuint shaderProgram, const CameraState& camera);
void updateDirectionalLight(GLuint shaderProgram, const ClientGame& game);
void updatePointLights(GLuint shaderProgram, const ClientGame& game);
void renderEntities(GLuint shaderProgram, ClientGame& game,
                    std::unordered_map<std::string, Model*>& models);
GLuint initSkyboxVAO();
void drawSkybox(GLuint skyboxShader, GLuint skyboxVAO, GLuint cubemapTexture,
                const CameraState& camera, const glm::mat4& projection);
