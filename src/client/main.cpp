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

  if (!game.audio.init()) {
    return EXIT_FAILURE;
  }

  InputKeys prevKeys = 0;

  std::thread networkThread(runNetworkLoop, std::ref(game), std::ref(network));

  auto lastTime = (float)glfwGetTime();

  while (!glfwWindowShouldClose(graphics.window)) {
    // add dt calculation at top of loop
    auto currentTime = (float)glfwGetTime();
    float dt = currentTime - lastTime;
    lastTime = currentTime;
    SIMPLE_PROFILE_FRAME_START();
    GPU_PROFILE_FRAME_BEGIN();

    SIMPLE_PROFILE_FRAME_START();
    if (game.snapshotDirty.load(std::memory_order_acquire)) {
      std::scoped_lock lock(game.snapshotMutex);
      syncToRender(game);
      game.snapshotDirty.store(false, std::memory_order_release);
    }

    float lx = 0, ly = 0, lz = 0;
    float fwdX = 0, fwdY = 1, fwdZ = 0;
    auto camView = game.renderRegistry.view<shared::Position, shared::Camera>();
    for (auto ent : camView) {
      auto& pos = camView.get<shared::Position>(ent);
      lx = pos.x;
      ly = pos.y;
      lz = pos.z;
      break;
    }

    game.audio.setListenerPosition(lx, ly, lz, fwdX, fwdY, fwdZ);
    updateSoundEmitters(game, lx, ly, lz, dt);  // pass dt

    graphics.render(game);
    {
      SIMPLE_PROFILE_SCOPE("Audio Update");
      game.audio.update(dt);
    }
    graphics.swap();
    GPU_PROFILE_FRAME_END();
    GPU_MEM_FRAME_END();
    glfwPollEvents();
    graphics.processDebugKeys();

    // ESC releases the cursor; left-click re-captures it.
    if (glfwGetKey(graphics.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetInputMode(graphics.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else if (glfwGetMouseButton(graphics.window, GLFW_MOUSE_BUTTON_LEFT) ==
                   GLFW_PRESS &&
               glfwGetInputMode(graphics.window, GLFW_CURSOR) ==
                   GLFW_CURSOR_NORMAL) {
      glfwSetInputMode(graphics.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    processInput(graphics.window, game, game.inputQueue, prevKeys);
    SIMPLE_PROFILE_FRAME_END("Client");
  }

  game.running.store(false, std::memory_order_release);
  networkThread.join();
  game.audio.shutdown();
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
