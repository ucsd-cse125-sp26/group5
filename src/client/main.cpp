// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
// clang-format on

#include <iostream>

#include "client_game.h"
#include "client_network.h"
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

  // An array of 3 vectors which represents 3 vertices
  static const GLfloat vertices[] = {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f,
                                     0.5f,  -0.5f, 0.0f, 0.0f, 1.0f, 0.0f,
                                     0.5f,  0.5f,  0.0f, 0.0f, 0.0f, 1.0f,
                                     -0.5f, 0.5f,  0.0f, 0.0f, 1.0f, 0.0f};
  static const GLuint indices[] = {0, 1, 2, 0, 3, 2};

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

  uint8_t prevKeys = 0;
  glUseProgram(shaderProgram);

  while (!glfwWindowShouldClose(window)) {
    network.poll(game);
    printEntityPositions(game);


    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glfwSwapBuffers(window);
    glfwPollEvents();

    processInput(window, network, prevKeys);
  }

  network.disconnect();
  network.shutdown();
  glfwTerminate();
  return 0;
}
