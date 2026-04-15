// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
// clang-format on

#include <iostream>

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

  // 24 vertices (4 per face) with per-face colors
  // clang-format off
  static const GLfloat vertices[] = {
      // Back face - red
      -0.5f, -0.5f, -0.5f,  0.8f, 0.2f, 0.2f,
       0.5f, -0.5f, -0.5f,  0.8f, 0.2f, 0.2f,
       0.5f,  0.5f, -0.5f,  0.8f, 0.2f, 0.2f,
      -0.5f,  0.5f, -0.5f,  0.8f, 0.2f, 0.2f,
      // Front face - green
      -0.5f, -0.5f,  0.5f,  0.2f, 0.8f, 0.2f,
       0.5f, -0.5f,  0.5f,  0.2f, 0.8f, 0.2f,
       0.5f,  0.5f,  0.5f,  0.2f, 0.8f, 0.2f,
      -0.5f,  0.5f,  0.5f,  0.2f, 0.8f, 0.2f,
      // Left face - blue
      -0.5f, -0.5f, -0.5f,  0.2f, 0.2f, 0.8f,
      -0.5f,  0.5f, -0.5f,  0.2f, 0.2f, 0.8f,
      -0.5f,  0.5f,  0.5f,  0.2f, 0.2f, 0.8f,
      -0.5f, -0.5f,  0.5f,  0.2f, 0.2f, 0.8f,
      // Right face - yellow
       0.5f, -0.5f, -0.5f,  0.8f, 0.8f, 0.2f,
       0.5f,  0.5f, -0.5f,  0.8f, 0.8f, 0.2f,
       0.5f,  0.5f,  0.5f,  0.8f, 0.8f, 0.2f,
       0.5f, -0.5f,  0.5f,  0.8f, 0.8f, 0.2f,
      // Bottom face - cyan
      -0.5f, -0.5f, -0.5f,  0.2f, 0.8f, 0.8f,
       0.5f, -0.5f, -0.5f,  0.2f, 0.8f, 0.8f,
       0.5f, -0.5f,  0.5f,  0.2f, 0.8f, 0.8f,
      -0.5f, -0.5f,  0.5f,  0.2f, 0.8f, 0.8f,
      // Top face - magenta
      -0.5f,  0.5f, -0.5f,  0.8f, 0.2f, 0.8f,
       0.5f,  0.5f, -0.5f,  0.8f, 0.2f, 0.8f,
       0.5f,  0.5f,  0.5f,  0.8f, 0.2f, 0.8f,
      -0.5f,  0.5f,  0.5f,  0.8f, 0.2f, 0.8f,
  };
  static const GLuint indices[] = {
       0,  1,  2,   0,  2,  3,
       4,  5,  6,   4,  6,  7,
       8,  9, 10,   8, 10, 11,
      12, 13, 14,  12, 14, 15,
      16, 17, 18,  16, 18, 19,
      20, 21, 22,  20, 22, 23,
  };
  // clang-format on

  GLuint VBO, VAO;

  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glBindVertexArray(VAO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat),
                        (void*)0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat),
                        (void*)(3 * sizeof(GLfloat)));
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);

  unsigned int EBO;
  glGenBuffers(1, &EBO);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);

  uint8_t prevKeys = 0;
  glUseProgram(shaderProgram);

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
      model = glm::rotate(model, i * 0.01f * glm::pi<GLfloat>(),
                          glm::vec3(0.0, 1.0, 0.0));
      model = glm::rotate(model, 1 * 0.25f * glm::pi<GLfloat>(),
                          glm::vec3(1.0, 0.0, 0.0));
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
      unsigned int transformLoc =
          glGetUniformLocation(shaderProgram, "transform");
      glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(transform));

      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
      glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
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
