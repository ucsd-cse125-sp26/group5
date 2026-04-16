// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
// clang-format on

#include <iostream>
#include <string>
#include <unordered_map>

#include "asset.h"
#include "client/util.h"
#include "client_game.h"
#include "client_network.h"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shaders.h"
#include "shared/assets.h"
#include "shared/components.h"
#include "shared/hello.h"

int main() {
  std::cout << "Hello World Client";
  shared::hello();

  ClientGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  ClientNetwork network;

  if (!network.connect("localhost", 7777)) {
    return EXIT_FAILURE;
  }

  registerClientHandlers(network);

  if (!glfwInit()) return -1;

  glfwWindowHint(GLFW_SAMPLES, 4);

  GLFWwindow* window =
      glfwCreateWindow(640, 480, "Hello World", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return -1;
  }

  glfwMakeContextCurrent(window);

  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported())
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

  int version = gladLoadGL(glfwGetProcAddress);
  printf("GL %d.%d\n", GLAD_VERSION_MAJOR(version),
         GLAD_VERSION_MINOR(version));

  // Load shaders
  GLuint shaderProgram =
      loadShaders("shaders/vertex.glsl", "shaders/fragment.glsl");

  std::unordered_map<std::string, Model*> models;
  for (const auto& asset : shared::ASSETS) {
    Model* m = asset.filename.empty() ? makeCubeModel()
                                      : loadModel(std::string(asset.filename));
    if (!m) {
      fprintf(stderr, "Failed to load asset '%s' (%s)\n",
              std::string(asset.name).c_str(),
              std::string(asset.filename).c_str());
      continue;
    }
    models[std::string(asset.name)] = m;
    printf("Loaded asset: %s\n", std::string(asset.name).c_str());
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);

  uint8_t prevKeys = 0;
  glUseProgram(shaderProgram);

  glUniform3f(glGetUniformLocation(shaderProgram, "dirLight.direction"), -0.3f,
              -1.0f, -0.4f);
  glUniform3f(glGetUniformLocation(shaderProgram, "dirLight.ambient"), 0.2f,
              0.2f, 0.2f);
  glUniform3f(glGetUniformLocation(shaderProgram, "dirLight.diffuse"), 0.8f,
              0.8f, 0.8f);
  glUniform3f(glGetUniformLocation(shaderProgram, "dirLight.specular"), 1.0f,
              1.0f, 1.0f);

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

  GLuint i = 0;

  glm::mat4 projection;
  projection =
      glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1,
                     GL_FALSE, glm::value_ptr(projection));

  while (!glfwWindowShouldClose(window)) {
    i += 1;
    network.poll(game);
    // printEntityPositions(game);

    glm::vec3 cameraPos(0.0f, 0.0f, 10.0f);
    glm::vec3 cameraTarget(0.0f, 0.0f, 0.0f);
    const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);

    auto selfIt = game.entityMap.find(game.myEntityId);
    if (selfIt != game.entityMap.end() && game.registry.valid(selfIt->second) &&
        game.registry.all_of<shared::Position, shared::Camera>(
            selfIt->second)) {
      const auto& p = game.registry.get<shared::Position>(selfIt->second);
      const auto& cam = game.registry.get<shared::Camera>(selfIt->second);

      glm::quat playerRot(p.qw, p.qx, p.qy, p.qz);
      glm::quat pitchRot =
          glm::angleAxis(cam.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
      glm::vec3 forward = playerRot * pitchRot * glm::vec3(0.0f, 1.0f, 0.0f);

      cameraPos = glm::vec3(p.x, p.y, p.z + cam.ht);
      cameraTarget = cameraPos + forward;
    } else {
      continue;
    }

    glm::mat4 viewMat = glm::lookAt(cameraPos, cameraTarget, worldUp);

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE,
                       glm::value_ptr(viewMat));
    glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), cameraPos.x,
                cameraPos.y, cameraPos.z);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    auto view =
        game.registry
            .view<shared::Entity, shared::Position, shared::RenderInfo>();
    for (auto ent : view) {
      auto& p = view.get<shared::Position>(ent);
      auto& renderInfo = view.get<shared::RenderInfo>(ent);
      auto& entity = view.get<shared::Entity>(ent);
      if (entity.id == selfIt->first) {
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
    glfwSwapBuffers(window);
    glfwPollEvents();

    // ESC releases the cursor; left-click re-captures it.
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) ==
                   GLFW_PRESS &&
               glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL) {
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    processInput(window, network, prevKeys);
  }

  network.disconnect();
  network.shutdown();
  glfwTerminate();
  return 0;
}
