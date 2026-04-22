#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "asset.h"
#include "client/client_game.h"
#include "client/shaders.h"
#include "glad/gl.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"

struct GLFWwindow;

struct CameraState {
  glm::vec3 position;
  glm::mat4 view;
};

std::optional<CameraState> computeCamera(const ClientGame& game);

struct Graphics {
  GLFWwindow* window = nullptr;
  std::optional<Shader> shader;
  std::optional<Shader> skyboxShader;
  std::unordered_map<std::string, Model*> models;
  std::unordered_map<std::string, Skybox> skyboxes;
  glm::mat4 projection{1.0f};

  bool load(int width, int height);
  void render(ClientGame& game);
  void swap();
  void destroy();
};
