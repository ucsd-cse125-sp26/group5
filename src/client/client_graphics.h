#pragma once

#include <optional>
#include <unordered_map>

#include "asset.h"
#include "client/client_game.h"
#include "glad/gl.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"

class Shader;

struct CameraState {
  glm::vec3 position;
  glm::mat4 view;
};

std::optional<CameraState> computeCamera(const ClientGame& game);
void initPointLights(const Shader& shader);
void setupCameraMatrix(const Shader& shader, const CameraState& camera);
void updateDirectionalLight(const Shader& shader, const ClientGame& game);
void updatePointLights(const Shader& shader, const ClientGame& game);
void renderEntities(const Shader& shader, ClientGame& game,
                    std::unordered_map<std::string, Model*>& models);
void drawSkybox(const Shader& shader, const Skybox& skybox,
                const CameraState& camera, const glm::mat4& projection);
