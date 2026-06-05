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
#include "shared/dev_spawn.h"
#include "shared/gpu_mem_profiler.h"
#include "shared/gpu_profiler.h"
#include "shared/hello.h"
#include "shared/log.h"
#include "shared/map_format.h"
#include "shared/map_gamelogic_layout.h"
#include "shared/puzzles/tangram/arena_layout.h"
#include "shared/simple_profiler.h"
#include "shared/sound_constants.h"
#include "shared/util.h"

void runNetworkLoop(ClientGame& game, ClientNetwork& network);
int main() {
  std::cout << "Hello World Client";
  shared::hello();
  shared::log::initFromEnvironment();

  ClientGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  ClientNetwork network;
  registerClientHandlers(network);

  Graphics graphics;
  if (!graphics.load(960, 600)) {
    return EXIT_FAILURE;
  }

  // load() captures the cursor for gameplay; release it for the menu.
  glfwSetInputMode(graphics.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

  char hostBuf[256] = "localhost";
  int portValue = 7777;
  std::string statusMsg;
  bool connected = false;
  while (!glfwWindowShouldClose(graphics.window)) {
    glfwPollEvents();
    auto result = graphics.renderServerMenuFrame(hostBuf, sizeof(hostBuf),
                                                 &portValue, statusMsg.c_str());
    if (result == Graphics::ServerMenuResult::Quit) break;
    if (result != Graphics::ServerMenuResult::Connect) continue;

    statusMsg = std::string("Connecting to ") + hostBuf + ":" +
                std::to_string(portValue) + "...";
    glfwPollEvents();
    graphics.renderServerMenuFrame(hostBuf, sizeof(hostBuf), &portValue,
                                   statusMsg.c_str());
    if (network.connect(hostBuf, static_cast<uint16_t>(portValue))) {
      connected = true;
      break;
    }
    // Tear down the partially-initialized ENet host before retrying so a
    // second connect() starts from a clean state.
    network.shutdown();
    statusMsg = std::string("Failed to connect to ") + hostBuf + ":" +
                std::to_string(portValue) + ". Try again.";
  }

  if (!connected) {
    return EXIT_SUCCESS;
  }

  // Init audio before the network thread so SEASON_MUSIC on connect is heard.
  if (!game.audio.init()) {
    LOG_DEBUG("Audio init failed; continuing without audio\n");
  }
  // Back to mouselook for gameplay.
  glfwSetInputMode(graphics.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  InputKeys prevKeys = 0;
  bool creditsDismissPrev = false;
  std::thread networkThread(runNetworkLoop, std::ref(game), std::ref(network));

  shared::map_gamelogic_layout::tryApplyMazeLayoutFromMapFile(
      (exeDir() / shared::DEFAULT_MAP_PATH).string(), game.mazeLayout);
  shared::map_gamelogic_layout::tryApplyTangramArenaFromMapFile(
      (exeDir() / shared::DEFAULT_MAP_PATH).string(), game.tangramArena);
  game.mazeLayout.applyHeightBoost();
  game.tangramArena.applyHeightBoost();

  if (shared::dev_spawn::kOverworldSpawn ==
      shared::dev_spawn::OverworldSpawn::Tangram) {
    applyPreset(graphics.settings, GraphicsPreset::Performance);
    graphics.settings.shadowsEnabled = false;
    graphics.settings.outlineMode = OutlineMode::None;
    LOG_DEBUG("[DevSpawn] Client fallback camera: tangram pad\n");
  } else {
    LOG_DEBUG("[DevSpawn] Client fallback camera: winter maze\n");
  }
  auto lastTime = (float)glfwGetTime();
  while (!glfwWindowShouldClose(graphics.window)) {
    auto currentTime = (float)glfwGetTime();
    float dt = currentTime - lastTime;
    lastTime = currentTime;
    SIMPLE_PROFILE_FRAME_START();
    GPU_PROFILE_FRAME_BEGIN();

    glfwPollEvents();
    graphics.processDebugKeys();
    if (game.snapshotDirty.load(std::memory_order_acquire)) {
      std::scoped_lock lock(game.snapshotMutex);
      syncToRender(game);
      game.snapshotDirty.store(false, std::memory_order_release);
    } else if (game.renderEntityMap.empty() && !game.networkEntityMap.empty()) {
      bootstrapClientWorldSnapshot(game);
    }

    updateWinterMazeWindowTitle(graphics.window, game);
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

    // Apply any server-driven video requests on the render/GL thread.
    VideoRequest videoReq;
    while (game.videoQueue.tryPop(videoReq))
      graphics.handleVideoRequest(videoReq);
    graphics.render(game, network);
    {
      SIMPLE_PROFILE_SCOPE("Audio Update");
      game.audio.update(dt);
    }
    graphics.swap();
    GPU_PROFILE_FRAME_END();
    GPU_MEM_FRAME_END();

    // Esc releases the captured cursor so you can use the Mac mouse again.
    bool escNow = glfwGetKey(graphics.window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (escNow && !graphics.keyEscapePrev) {
      glfwSetInputMode(graphics.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    graphics.keyEscapePrev = escNow;

    // H toggles the graphics settings menu (cursor follows menu state).
    bool menuKeyNow = glfwGetKey(graphics.window, GLFW_KEY_H) == GLFW_PRESS;
    if (menuKeyNow && !graphics.keySettingsMenuPrev) {
      graphics.settingsMenuOpen = !graphics.settingsMenuOpen;
    }
    graphics.keySettingsMenuPrev = menuKeyNow;

    // Ctrl+Shift+\ toggles the demo debug control panel. Deliberately a
    // nonintuitive chord so it can't be hit by accident on stage; debounced on
    // the backslash key.
    bool dbgChord =
        (glfwGetKey(graphics.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
         glfwGetKey(graphics.window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) &&
        (glfwGetKey(graphics.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
         glfwGetKey(graphics.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) &&
        glfwGetKey(graphics.window, GLFW_KEY_BACKSLASH) == GLFW_PRESS;
    if (dbgChord && !graphics.keyDebugPanelPrev) {
      graphics.debugPanelOpen = !graphics.debugPanelOpen;
    }
    graphics.keyDebugPanelPrev = dbgChord;

    // Enter dismisses the credits roll back to the overworld view. The server
    // never froze gameplay, so this is a purely local view/music swap.
    bool dismissNow = glfwGetKey(graphics.window, GLFW_KEY_ENTER) == GLFW_PRESS;
    if (dismissNow && !creditsDismissPrev &&
        game.currentGameState == shared::GameStateType::CREDITS) {
      game.currentGameState = shared::GameStateType::OVERWORLD;
      graphics.creditsStartTime = -1.0;
      game.audio.stopGlobalLoop(
          static_cast<uint32_t>(shared::SoundId::CREDITS_MUSIC));
    }
    creditsDismissPrev = dismissNow;

    // Sync cursor mode whenever a cursor-freeing menu opens/closes — settings
    // (H) or the debug panel — whether toggled by key or by an in-UI Close box.
    bool anyMenuOpen = graphics.settingsMenuOpen || graphics.debugPanelOpen;
    if (anyMenuOpen != graphics.prevSyncedMenuOpen) {
      glfwSetInputMode(graphics.window, GLFW_CURSOR,
                       anyMenuOpen ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
      graphics.prevSyncedMenuOpen = anyMenuOpen;
    }

    // Click-to-recapture for edge cases (e.g. cursor was freed externally).
    if (!anyMenuOpen && !ImGui::GetIO().WantCaptureMouse &&
        glfwGetMouseButton(graphics.window, GLFW_MOUSE_BUTTON_LEFT) ==
            GLFW_PRESS &&
        glfwGetInputMode(graphics.window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL) {
      glfwSetInputMode(graphics.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    processInput(
        graphics.window, game, game.inputQueue, prevKeys,
        graphics.debugChannel != DebugChannel::Off || graphics.debugPanelOpen);
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
    network.drainDebugQueue(game.debugQueue);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  network.disconnect();
  network.shutdown();
}
