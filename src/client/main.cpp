// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
// clang-format on

#include <iostream>

#include "client/client_graphics.h"
#include "client_game.h"
#include "client_network.h"
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

  Graphics graphics;
  if (!graphics.load(960, 600)) {
    return EXIT_FAILURE;
  }

  InputKeys prevKeys = 0;

  while (!glfwWindowShouldClose(graphics.window)) {
    network.poll(game);
    graphics.render(game);
    graphics.swap();
    glfwPollEvents();

    // ESC releases the cursor; left-click re-captures it.
    if (glfwGetKey(graphics.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetInputMode(graphics.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else if (glfwGetMouseButton(graphics.window, GLFW_MOUSE_BUTTON_LEFT) ==
                   GLFW_PRESS &&
               glfwGetInputMode(graphics.window, GLFW_CURSOR) ==
                   GLFW_CURSOR_NORMAL) {
      glfwSetInputMode(graphics.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    processInput(graphics.window, network, prevKeys);
  }

  graphics.destroy();
  network.disconnect();
  network.shutdown();
  return 0;
}
