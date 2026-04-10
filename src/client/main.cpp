// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
// clang-format on

#include <iostream>

#include "client_game.h"
#include "client_network.h"
#include "shared/component_registry.h"
#include "shared/hello.h"

int main() {
  std::cout << "Hello World Client";
  shared::hello();
  shared::registerAllSyncedComponents();

  ClientGame game;
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

  uint8_t prevKeys = 0;

  while (!glfwWindowShouldClose(window)) {
    network.poll(game);
    printEntityPositions(game);

    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glfwSwapBuffers(window);
    glfwPollEvents();

    processInput(window, network, prevKeys);
  }

  network.disconnect();
  network.shutdown();
  glfwTerminate();
  return 0;
}
