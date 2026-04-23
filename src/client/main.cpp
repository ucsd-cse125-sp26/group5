// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
// clang-format on

#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>

#include "asset.h"
#include "client/client_graphics.h"
#include "client/util.h"
#include "client_game.h"
#include "client_network.h"
#include "glm/gtc/type_ptr.hpp"
#include "shaders.h"
#include "shared/assets.h"
#include "shared/components.h"
#include "shared/hello.h"

void test_step(ClientGame& game);
void runNetworkLoop(ClientGame& game, ClientNetwork& network);
int main() {
  std::cout << "Hello World Client";
  shared::hello();

  ClientGame game;
  game.componentRegistry = shared::createDefaultRegistry();
  // test_step(game);
  ClientNetwork network;

  if (!network.connect("localhost", 7777)) {
    return EXIT_FAILURE;
  }

  registerClientHandlers(network);

  if (!glfwInit()) return -1;

  glfwWindowHint(GLFW_SAMPLES, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

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

  InputKeys prevKeys = 0;
  glUseProgram(shaderProgram);

  initPointLights(shaderProgram);

  GLuint i = 0;

  glm::mat4 projection;
  projection =
      glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1,
                     GL_FALSE, glm::value_ptr(projection));
  std::thread networkThread(runNetworkLoop, std::ref(game), std::ref(network));
  while (!glfwWindowShouldClose(window)) {
    i += 1;
    // this is not race condition, because the snapshotDirty is only set to true
    // in the network thread set to false in the main thread
    if (game.snapshotDirty.load(std::memory_order_acquire)) {
      std::scoped_lock lock(game.snapshotMutex);
      syncToRender(game);
      game.snapshotDirty.store(false, std::memory_order_release);
    }

    if (!setupCameraMatrix(shaderProgram, game)) {
      glfwSwapBuffers(window);
      glfwPollEvents();
      continue;
    }

    updateDirectionalLight(shaderProgram, game);
    updatePointLights(shaderProgram, game);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    renderEntities(shaderProgram, game, models);
    
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

    processInput(window, game.inputQueue, prevKeys);
  }
  game.running.store(false, std::memory_order_release);
  networkThread.join();
  glfwTerminate();
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