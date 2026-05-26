// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
// clang-format on

#include <cassert>
#include <iostream>
#include <thread>

#include "client/client_graphics.h"
#include "client_game.h"
#include "client_network.h"
#include "imgui.h"
#include "shared/gpu_mem_profiler.h"
#include "shared/gpu_profiler.h"
#include "shared/hello.h"
#include "shared/simple_profiler.h"

void runNetworkLoop(ClientGame& game, ClientNetwork& network);
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

  std::thread networkThread(runNetworkLoop, std::ref(game), std::ref(network));
  while (!glfwWindowShouldClose(graphics.window)) {
    SIMPLE_PROFILE_FRAME_START();
    GPU_PROFILE_FRAME_BEGIN();

    if (game.snapshotDirty.load(std::memory_order_acquire)) {
      std::scoped_lock lock(game.snapshotMutex);
      syncToRender(game);
      game.snapshotDirty.store(false, std::memory_order_release);
    }

    graphics.render(game);
    graphics.swap();
    GPU_PROFILE_FRAME_END();
    GPU_MEM_FRAME_END();
    glfwPollEvents();
    graphics.processDebugKeys();

    // ESC toggles the settings menu (and the cursor follows menu state).
    bool escNow = glfwGetKey(graphics.window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (escNow && !graphics.keyEscapePrev) {
      graphics.settingsMenuOpen = !graphics.settingsMenuOpen;
    }
    graphics.keyEscapePrev = escNow;

    // Sync cursor mode whenever the menu state changes — whether from ESC or
    // the in-UI Close button (which flipped the flag during render).
    if (graphics.settingsMenuOpen != graphics.prevSyncedMenuOpen) {
      glfwSetInputMode(graphics.window, GLFW_CURSOR,
                       graphics.settingsMenuOpen ? GLFW_CURSOR_NORMAL
                                                 : GLFW_CURSOR_DISABLED);
      graphics.prevSyncedMenuOpen = graphics.settingsMenuOpen;
    }

    // Click-to-recapture for edge cases (e.g. cursor was freed externally).
    if (!graphics.settingsMenuOpen && !ImGui::GetIO().WantCaptureMouse &&
        glfwGetMouseButton(graphics.window, GLFW_MOUSE_BUTTON_LEFT) ==
            GLFW_PRESS &&
        glfwGetInputMode(graphics.window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL) {
      glfwSetInputMode(graphics.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    if (!graphics.settingsMenuOpen) {
      processInput(graphics.window, game, game.inputQueue, prevKeys);
    }
    SIMPLE_PROFILE_FRAME_END("Client");
  }

  game.running.store(false, std::memory_order_release);
  networkThread.join();
  return 0;
}

void runNetworkLoop(ClientGame& game, ClientNetwork& network) {
  while (game.running.load(std::memory_order_acquire)) {
    {
      std::scoped_lock lock(game.snapshotMutex);
      network.poll(game);
    }
    network.drainInputQueue(game.inputQueue);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  network.disconnect();
  network.shutdown();
}
