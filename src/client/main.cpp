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
#include "shared/map_format.h"
#include "shared/map_gamelogic_layout.h"
#include "shared/simple_profiler.h"
#include "shared/puzzles/tangram/arena_layout.h"
#include "shared/dev_spawn.h"
#include "shared/util.h"

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

  // Service ENet while graphics.load() parses the huge map glb (~130 meshes).
  // Otherwise the client never calls enet_host_service for minutes and the
  // connection times out (looks like "last player disconnects" when the
  // slowest loader finishes last). Same for tryApplyMazeLayoutFromMapFile.
  InputKeys prevKeys = 0;
  std::thread networkThread(runNetworkLoop, std::ref(game), std::ref(network));

  Graphics graphics;
  if (!graphics.load(960, 600)) {
    game.running.store(false, std::memory_order_release);
    networkThread.join();
    return EXIT_FAILURE;
  }

  shared::map_gamelogic_layout::tryApplyMazeLayoutFromMapFile(
      (exeDir() / shared::DEFAULT_MAP_PATH).string(), game.mazeLayout);
  shared::map_gamelogic_layout::tryApplyTangramArenaFromMapFile(
      (exeDir() / shared::DEFAULT_MAP_PATH).string(), game.tangramArena);
  game.mazeLayout.applyHeightBoost();
  game.tangramArena.applyHeightBoost();

  if (shared::dev_spawn::kOverworldSpawn ==
      shared::dev_spawn::OverworldSpawn::Tangram) {
    printf("[DevSpawn] Client fallback camera: tangram pad\n");
  } else {
    printf("[DevSpawn] Client fallback camera: winter maze\n");
  }

  while (!glfwWindowShouldClose(graphics.window)) {
    SIMPLE_PROFILE_FRAME_START();
    GPU_PROFILE_FRAME_BEGIN();

    glfwPollEvents();
    graphics.processDebugKeys();

    if (game.snapshotDirty.load(std::memory_order_acquire)) {
      std::scoped_lock lock(game.snapshotMutex);
      syncToRender(game);
      game.snapshotDirty.store(false, std::memory_order_release);
    } else if (game.renderEntityMap.empty() &&
               !game.networkEntityMap.empty()) {
      bootstrapClientWorldSnapshot(game);
    }

    updateWinterMazeWindowTitle(graphics.window, game);
    graphics.render(game, network);
    graphics.swap();
    GPU_PROFILE_FRAME_END();
    GPU_MEM_FRAME_END();

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
