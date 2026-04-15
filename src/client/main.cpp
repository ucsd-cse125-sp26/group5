// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
// clang-format on

#include <iostream>
#include <string>

#include "asset.h"
#include "client_game.h"
#include "client_network.h"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shaders.h"
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

  int version = gladLoadGL(glfwGetProcAddress);
  printf("GL %d.%d\n", GLAD_VERSION_MAJOR(version),
         GLAD_VERSION_MINOR(version));

  // Load shaders
  GLuint shaderProgram =
      loadShaders("shaders/vertex.glsl", "shaders/fragment.glsl");

  // Model* cube = loadModel("assets/rebecca/CSE125Test.glb");
  Model* cube = loadModel("assets/bear/bear_full.obj");

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

  while (!glfwWindowShouldClose(window)) {
    i += 1;
    network.poll(game);
    // printEntityPositions(game);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    auto view = game.registry.view<shared::Entity, shared::Position>();
    for (auto ent : view) {
      auto& p = view.get<shared::Position>(ent);
      glm::mat4 model =
          glm::translate(glm::identity<glm::mat4>(), glm::vec3(p.x, p.y, 0.0));
      model = glm::rotate(model, i * 0.001f * glm::pi<GLfloat>(),
                          glm::vec3(0.0, 1.0, 0.0));
      // model = glm::rotate(model, 1 * 0.25f * glm::pi<GLfloat>(),
      //                    glm::vec3(1.0, 0.0, 0.0));
      model = glm::scale(model, glm::vec3(0.1f));
      // trans = glm::rotate(trans, i * 0.01f * glm::pi<GLfloat>(),
      // glm::vec3(0.0, 0.0, 1.0));

      glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 10.0f);

      glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
      glm::vec3 cameraDirection = glm::normalize(cameraPos - cameraTarget);

      glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
      glm::vec3 cameraRight = glm::normalize(glm::cross(up, cameraDirection));

      glm::vec3 cameraUp = glm::cross(cameraDirection, cameraRight);

      glm::mat4 view = glm::identity<glm::mat4>();

      view = glm::lookAt(cameraPos, cameraTarget, cameraUp);

      glm::mat4 projection;
      projection =
          glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);

      glm::mat4 transform = projection * view * model;
      glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1,
                         GL_FALSE, glm::value_ptr(projection));
      glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1,
                         GL_FALSE, glm::value_ptr(view));
      glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), cameraPos.x,
                  cameraPos.y, cameraPos.z);
      Draw(shaderProgram, *cube, model);
    }
    glfwSwapBuffers(window);
    glfwPollEvents();

    processInput(window, network, prevKeys);
  }

  network.disconnect();
  network.shutdown();
  glfwTerminate();
  return 0;
}
