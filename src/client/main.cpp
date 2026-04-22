// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
// clang-format on

#include <iostream>
#include <string>
#include <unordered_map>

#include "asset.h"
#include "client/client_graphics.h"
#include "client_game.h"
#include "client_network.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "shaders.h"
#include "shared/assets.h"
#include "shared/components.h"
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
  Shader shader("shaders/vertex.glsl", "shaders/fragment.glsl");
  Shader skyboxShader("shaders/vertex_skybox.glsl",
                      "shaders/fragment_skybox.glsl");

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

  GLuint skyboxVAO = initSkyboxVAO();
  GLuint cubemapTexture = loadCubemap({
      "assets/skybox-1/px.png",
      "assets/skybox-1/nx.png",
      "assets/skybox-1/py.png",
      "assets/skybox-1/ny.png",
      "assets/skybox-1/pz.png",
      "assets/skybox-1/nz.png",
  });

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_MULTISAMPLE);

  InputKeys prevKeys = 0;
  shader.use();

  initPointLights(shader);
  GLuint i = 0;

  glm::mat4 projection =
      glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
  shader.setMat4("projection", projection);

  while (!glfwWindowShouldClose(window)) {
    i += 1;
    network.poll(game);
    // printEntityPositions(game);
    auto camera = computeCamera(game);
    if (!camera) {
      continue;
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shader.use();
    setupCameraMatrix(shader, *camera);
    updateDirectionalLight(shader, game);
    updatePointLights(shader, game);
    renderEntities(shader, game, models);
    drawSkybox(skyboxShader, skyboxVAO, cubemapTexture, *camera, projection);
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

    processInput(window, network, prevKeys);
  }

  shader.destroy();
  skyboxShader.destroy();

  network.disconnect();
  network.shutdown();
  glfwTerminate();
  return 0;
}
