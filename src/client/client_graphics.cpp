// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
// clang-format on

#include "client_graphics.h"

#include <cmath>
#include <iostream>
#include <string>

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shared/assets.h"
#include "shared/components.h"
#include "shared/map_format.h"
#include "shared/simple_profiler.h"

// Skybox images use Y-up; the game uses Z-up.
// cubemap->game: X->X, Y->Z, Z->-Y  (column-major)
static const glm::mat3 kCubemapToGame(1, 0, 0, 0, 0, 1, 0, -1, 0);

// Resolution of the directional shadow map. 2048² gives crisp shadows at
// modest VRAM cost; bump if the scene grows larger.
static constexpr int kDirShadowMapSize = 2048;

// Point-light cubemap array. 4 lights × 6 faces × 1024² depth ≈ 24 MB.
static constexpr int kPointShadowSize = 1024;
static constexpr int kMaxPointLights = 4;
static constexpr int kPointShadowLayers = kMaxPointLights * 6;
// Linear-distance encoding range for point-light shadow depth.
static constexpr float kPointShadowNear = 0.1f;
static constexpr float kPointShadowFar = 50.0f;

// CPU-side mirror of one PointLight uniform struct.
struct LightUpload {
  glm::vec3 position;
  float constant;
  float linear;
  float quadratic;
  glm::vec3 ambient;
  glm::vec3 diffuse;
  glm::vec3 specular;
  int shadowIdx;  // -1 = non-shadow-casting
};

static constexpr int kMaxLightingShaderLights = 32;

// Iterates the ECS PointLight view, copies up to kMaxLightingShaderLights
// into out, and assigns the first kMaxPointLights with castsShadow=true to
// shadow slots 0..(kMaxPointLights-1). Returns the active light count.
static int collectPointLights(const ClientGame& game,
                               LightUpload out[kMaxLightingShaderLights]) {
  int count = 0;
  int shadowSlot = 0;
  auto view = game.renderRegistry.view<shared::PointLight>();
  for (auto ent : view) {
    if (count >= kMaxLightingShaderLights) break;
    const auto& pl = view.get<shared::PointLight>(ent);
    LightUpload& l = out[count++];
    l.position = glm::vec3(pl.px, pl.py, pl.pz);
    l.constant = pl.constant;
    l.linear = pl.linear;
    l.quadratic = pl.quadratic;
    l.ambient = glm::vec3(pl.ambientR, pl.ambientG, pl.ambientB);
    l.diffuse = glm::vec3(pl.diffuseR, pl.diffuseG, pl.diffuseB);
    l.specular = glm::vec3(pl.specularR, pl.specularG, pl.specularB);
    l.shadowIdx =
        (pl.castsShadow && shadowSlot < kMaxPointLights) ? shadowSlot++ : -1;
  }
  return count;
}

// Pushes the collected lights into the deferred lighting shader's uniform
// arrays.
static void uploadPointLights(const Shader& shader,
                               const LightUpload* lights, int count) {
  shader.setInt("numPointLights", count);
  for (int i = 0; i < count; ++i) {
    std::string base = "pointLights[" + std::to_string(i) + "].";
    shader.setVec3(base + "position", lights[i].position);
    shader.setFloat(base + "constant", lights[i].constant);
    shader.setFloat(base + "linear", lights[i].linear);
    shader.setFloat(base + "quadratic", lights[i].quadratic);
    shader.setVec3(base + "ambient", lights[i].ambient);
    shader.setVec3(base + "diffuse", lights[i].diffuse);
    shader.setVec3(base + "specular", lights[i].specular);
    shader.setInt(base + "shadowIdx", lights[i].shadowIdx);
  }
}

static void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
  auto* g = static_cast<Graphics*>(glfwGetWindowUserPointer(w));
  if (g) g->resizeBuffers(width, height);
}

// Returns the active directional light direction (ECS override beats scene
// default). If no directional light is configured, returns a sensible
// fallback so the shadow pass still runs without crashing.
static glm::vec3 directionalLightDir(const ClientGame& game) {
  auto dlView = game.renderRegistry.view<shared::DirectionalLight>();
  for (auto ent : dlView) {
    const auto& dl = dlView.get<shared::DirectionalLight>(ent);
    return glm::vec3(dl.dirX, dl.dirY, dl.dirZ);
  }
  auto sceneView = game.renderRegistry.view<shared::Scene>();
  for (auto ent : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(ent);
    if (auto* info = shared::findScene(scene.name)) {
      return glm::vec3(info->dirX, info->dirY, info->dirZ);
    }
  }
  return glm::vec3(0.3f, 1.0f, -0.4f);
}

// Builds 24 light-space matrices for the point-light cubemap-array shadow
// pass. Active light slots get real per-face matrices; inactive slots get a
// "kill" matrix that produces clip-space z > 1, so emitted geometry is
// clipped and never writes to those layers (their cleared depth=1.0
// remains, meaning "not in shadow" when sampled). Also writes the per-light
// world-space positions into outPositions, padded with origins for
// inactive slots.
//
// Reads shadow-slot assignments from the LightUpload array so that the
// lighting shader and the shadow shader agree on which cubemap layer each
// shadow-casting light owns.
static void computePointShadowMatrices(const LightUpload* lights, int count,
                                       glm::mat4 outMatrices[kPointShadowLayers],
                                       glm::vec3 outPositions[kMaxPointLights]) {
  // Cube-face directions/ups. Same convention learnopengl uses; the cube
  // map is its own coordinate system, independent of the world's up axis.
  static const struct {
    glm::vec3 dir;
    glm::vec3 up;
  } faces[6] = {
      {{1, 0, 0}, {0, -1, 0}},   // +X
      {{-1, 0, 0}, {0, -1, 0}},  // -X
      {{0, 1, 0}, {0, 0, 1}},    // +Y
      {{0, -1, 0}, {0, 0, -1}},  // -Y
      {{0, 0, 1}, {0, -1, 0}},   // +Z
      {{0, 0, -1}, {0, -1, 0}},  // -Z
  };

  // Kill matrix: any input → clip space (0, 0, 2, 1) → NDC z = 2 → clipped.
  glm::mat4 kill(0.0f);
  kill[3] = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
  for (int i = 0; i < kPointShadowLayers; ++i) outMatrices[i] = kill;
  for (int i = 0; i < kMaxPointLights; ++i)
    outPositions[i] = glm::vec3(0.0f);

  glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f,
                                    kPointShadowNear, kPointShadowFar);

  for (int i = 0; i < count; ++i) {
    int slot = lights[i].shadowIdx;
    if (slot < 0 || slot >= kMaxPointLights) continue;
    const glm::vec3& p = lights[i].position;
    outPositions[slot] = p;
    for (int f = 0; f < 6; ++f) {
      outMatrices[slot * 6 + f] =
          proj * glm::lookAt(p, p + faces[f].dir, faces[f].up);
    }
  }
}

// Builds a light-space matrix that wraps an ortho frustum around the camera.
// The frustum follows the camera so distant objects get shadows too without
// burning the whole shadow map on the world origin.
static glm::mat4 computeDirectionalLightMatrix(const glm::vec3& cameraPos,
                                               const glm::vec3& lightDir) {
  glm::vec3 dir = glm::normalize(lightDir);
  // Place the virtual sun far enough behind the camera that the entire
  // frustum stays in front of it. 30 units works for the current scene scale.
  glm::vec3 lightPos = cameraPos - dir * 30.0f;
  glm::vec3 up = glm::abs(dir.z) > 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                        : glm::vec3(0.0f, 0.0f, 1.0f);
  glm::mat4 view = glm::lookAt(lightPos, cameraPos, up);
  // ±20 covers the visible play area; near=1, far=80 reaches across.
  glm::mat4 proj = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 80.0f);
  return proj * view;
}

std::optional<CameraState> computeCamera(const ClientGame& game) {
  auto selfIt = game.renderEntityMap.find(game.renderEntityId);
  if (selfIt == game.renderEntityMap.end() ||
      !game.renderRegistry.valid(selfIt->second) ||
      !game.renderRegistry.all_of<shared::Position, shared::Camera>(
          selfIt->second)) {
    return std::nullopt;
  }
  const auto& p = game.renderRegistry.get<shared::Position>(selfIt->second);
  const auto& cam = game.renderRegistry.get<shared::Camera>(selfIt->second);

  const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
  glm::quat playerRot(p.qw, p.qx, p.qy, p.qz);
  // Extract yaw only so entity pitch/roll doesn't tilt the camera.
  glm::vec3 flat = playerRot * glm::vec3(0.0f, 1.0f, 0.0f);
  flat.z = 0.0f;
  flat = glm::normalize(flat);
  float yaw = std::atan2(-flat.x, flat.y);
  glm::quat yawRot = glm::angleAxis(yaw, glm::vec3(0.0f, 0.0f, 1.0f));
  glm::quat pitchRot = glm::angleAxis(cam.pitch, glm::vec3(1.0f, 0.0f, 0.0f));
  glm::vec3 forward = yawRot * pitchRot * glm::vec3(0.0f, 1.0f, 0.0f);

  glm::vec3 pos = glm::vec3(p.x, p.y, p.z + cam.ht);
  glm::mat4 view = glm::lookAt(pos, pos + forward, worldUp);
  return CameraState{.position = pos, .view = view};
}

static void setupCameraMatrix(const Shader& shader, const CameraState& camera) {
  shader.setMat4("view", camera.view);
  shader.setVec3("viewPos", camera.position.x, camera.position.y,
                 camera.position.z);
}

static const shared::SceneInfo* currentScene(const ClientGame& game) {
  auto sceneView = game.renderRegistry.view<shared::Scene>();
  for (auto ent : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(ent);
    auto* info = shared::findScene(scene.name);
    if (info) return info;
  }
  return nullptr;
}

static void updateDirectionalLight(const Shader& shader,
                                   const ClientGame& game) {
  // ECS DirectionalLight overrides the scene's default. First entity wins;
  // subsequent ones are ignored (shader has only one dirLight slot).
  auto dlView = game.renderRegistry.view<shared::DirectionalLight>();
  for (auto ent : dlView) {
    const auto& dl = dlView.get<shared::DirectionalLight>(ent);
    shader.setVec3("dirLight.direction", dl.dirX, dl.dirY, dl.dirZ);
    shader.setVec3("dirLight.ambient", dl.ambientR, dl.ambientG, dl.ambientB);
    shader.setVec3("dirLight.diffuse", dl.diffuseR, dl.diffuseG, dl.diffuseB);
    shader.setVec3("dirLight.specular", dl.specularR, dl.specularG,
                   dl.specularB);
    return;
  }
  auto* info = currentScene(game);
  if (!info) return;
  shader.setVec3("dirLight.direction", info->dirX, info->dirY, info->dirZ);
  shader.setVec3("dirLight.ambient", info->ambientR, info->ambientG,
                 info->ambientB);
  shader.setVec3("dirLight.diffuse", info->diffuseR, info->diffuseG,
                 info->diffuseB);
  shader.setVec3("dirLight.specular", info->specularR, info->specularG,
                 info->specularB);
}

static void drawSkybox(const Shader& shader, const Skybox& skybox,
                       const CameraState& camera, const glm::mat4& projection) {
  glm::mat4 skyboxView = glm::mat4(glm::mat3(camera.view) * kCubemapToGame);

  glDepthFunc(GL_LEQUAL);
  shader.use();
  shader.setMat4("view", skyboxView);
  shader.setMat4("projection", projection);

  glBindVertexArray(skybox.vao);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, skybox.cubemapTexture);
  glDrawArrays(GL_TRIANGLES, 0, 36);
  glBindVertexArray(0);
  glDepthFunc(GL_LESS);
}

static void renderEntities(const Shader& shader, ClientGame& game,
                           std::unordered_map<std::string, Model*>& models,
                           bool forShadowPass = false) {
  auto view = game.renderRegistry
                  .view<shared::Entity, shared::Position, shared::RenderInfo>();
  for (auto ent : view) {
    auto& p = view.get<shared::Position>(ent);
    auto& renderInfo = view.get<shared::RenderInfo>(ent);
    auto& entity = view.get<shared::Entity>(ent);
    if (forShadowPass) {
      // Light-marker meshes (the visible cube on a point/dir light) sit at
      // the light's origin; rendering them into the shadow map makes the
      // light shadow itself. Skip any entity whose render mesh represents
      // a light source.
      if (game.renderRegistry.any_of<shared::PointLight,
                                      shared::DirectionalLight>(ent))
        continue;
    } else {
      // Main pass: skip the camera entity (we'd see our own model
      // first-person otherwise).
      if (entity.id == game.renderEntityId) continue;
    }
    auto it = models.find(renderInfo.modelName);
    if (it == models.end() || !it->second) continue;
    Model* modelAsset = it->second;
    glm::quat rotation = glm::quat(p.qw, p.qx, p.qy, p.qz);
    auto model = glm::identity<glm::mat4>();
    model = glm::translate(model, glm::vec3(p.x, p.y, p.z));
    model = glm::scale(model,
                       glm::vec3(renderInfo.sx, renderInfo.sy, renderInfo.sz));
    model = model * glm::mat4_cast(rotation) *
            glm::mat4_cast(modelAsset->orientation);

    Draw(shader, *modelAsset, model);
  }
}

bool Graphics::load(int width, int height) {
  if (!glfwInit()) return false;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  window = glfwCreateWindow(width, height, "Hello World", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);  // vsync: cap to display refresh rate

  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported())
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

  // Stash this Graphics instance on the window so the framebuffer-size
  // callback can route resize events back into resizeBuffers.
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

  // Capture windowed geometry for fullscreen restore.
  glfwGetWindowPos(window, &windowedX, &windowedY);
  glfwGetWindowSize(window, &windowedW, &windowedH);
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

  int version = gladLoadGL(glfwGetProcAddress);
  printf("GL %d.%d\n", GLAD_VERSION_MAJOR(version),
         GLAD_VERSION_MINOR(version));

  gbufferShader.emplace("shaders/vertex_gbuffer.glsl",
                        "shaders/fragment_gbuffer.glsl");
  lightingShader.emplace("shaders/vertex_present.glsl",
                         "shaders/fragment_lighting_deferred.glsl");
  skyboxShader.emplace("shaders/vertex_skybox.glsl",
                       "shaders/fragment_skybox.glsl");
  presentShader.emplace("shaders/vertex_present.glsl",
                        "shaders/fragment_fxaa.glsl");
  blurShader.emplace("shaders/vertex_present.glsl",
                     "shaders/fragment_blur.glsl");
  tonemapShader.emplace("shaders/vertex_present.glsl",
                        "shaders/fragment_tonemap.glsl");
  shadowDirShader.emplace("shaders/vertex_shadow_dir.glsl",
                          "shaders/fragment_shadow_dir.glsl");
  shadowPointShader.emplace("shaders/vertex_shadow_point.glsl",
                            "shaders/fragment_shadow_point.glsl",
                            "shaders/geometry_shadow_point.glsl");
  debugOverlay.emplace("shaders/vertex_present.glsl",
                       "shaders/fragment_debug_overlay.glsl");

  // Allocate the directional shadow map once; size is independent of window
  // size so resizeBuffers leaves it alone.
  glGenFramebuffers(1, &dirShadowFBO);
  glGenTextures(1, &dirShadowMap);
  glBindTexture(GL_TEXTURE_2D, dirShadowMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kDirShadowMapSize,
               kDirShadowMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  // Outside the frustum returns depth=1.0 so DirShadowFactor reports unlit.
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, white);
  glBindFramebuffer(GL_FRAMEBUFFER, dirShadowFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         dirShadowMap, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "dirShadowFBO incomplete\n");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Point-light cubemap array shadow target. One depth attachment, 24
  // layer-faces (4 cubes × 6 faces). The whole array is bound to the FBO
  // via glFramebufferTexture so the GS can pick layers via gl_Layer.
  glGenFramebuffers(1, &pointShadowFBO);
  glGenTextures(1, &pointShadowMaps);
  glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, pointShadowMaps);
  glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT24,
               kPointShadowSize, kPointShadowSize, kPointShadowLayers, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S,
                  GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T,
                  GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R,
                  GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
  glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, pointShadowMaps,
                       0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "pointShadowFBO incomplete\n");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Empty VAO for fullscreen-triangle draws (positions synthesized in vert
  // shader via gl_VertexID).
  glGenVertexArrays(1, &fullscreenVAO);

  for (const auto& asset : shared::ASSETS) {
    Model* m = asset.cubeSpec ? makeCubeModel(*asset.cubeSpec)
                              : loadModel(std::string(asset.filename));
    if (!m) {
      fprintf(stderr, "Failed to load asset '%s' (%s)\n",
              std::string(asset.name).c_str(),
              std::string(asset.filename).c_str());
      continue;
    }
    m->orientation = glm::quat(asset.qw, asset.qx, asset.qy, asset.qz);
    models[std::string(asset.name)] = m;
    printf("Loaded asset: %s\n", std::string(asset.name).c_str());
  }

  // Register per-node sub-models from the active map. Server-side map_loader
  // spawns entities whose RenderInfo.modelName uses these same keys.
  auto mapModels = loadMapModels(shared::DEFAULT_MAP_PATH);
  for (auto& [key, m] : mapModels) {
    models[key] = m;
    printf("Loaded map sub-model: %s\n", key.c_str());
  }

  for (const auto& sc : shared::SCENES) {
    std::string dir = std::string(sc.skyboxDirectory);
    if (skyboxes.find(dir) == skyboxes.end()) {
      skyboxes[dir] = loadSkybox(dir);
      printf("Loaded skybox: %s (%s)\n", std::string(sc.name).c_str(),
             dir.c_str());
    }
  }

  glEnable(GL_DEPTH_TEST);

  resizeBuffers(fbWidth, fbHeight);
  initShaderUniforms();

  return true;
}

void Graphics::resizeBuffers(int width, int height) {
  if (width <= 0 || height <= 0) return;
  fbWidth = width;
  fbHeight = height;
  glViewport(0, 0, fbWidth, fbHeight);
  projection = glm::perspective(
      glm::radians(45.0f),
      static_cast<float>(fbWidth) / static_cast<float>(fbHeight), 0.1f, 100.0f);
  if (gbufferShader && gbufferShader->valid()) {
    gbufferShader->use();
    gbufferShader->setMat4("projection", projection);
  }

  // G-buffer attachments. Position and normal are RGBA16F so we don't lose
  // precision on world-space coords; albedo+spec and emissive are RGBA8.
  if (!gBufferFBO) glGenFramebuffers(1, &gBufferFBO);
  if (!gPosition) glGenTextures(1, &gPosition);
  if (!gNormal) glGenTextures(1, &gNormal);
  if (!gAlbedo) glGenTextures(1, &gAlbedo);
  if (!gSpecular) glGenTextures(1, &gSpecular);
  if (!gEmissive) glGenTextures(1, &gEmissive);
  if (!gBufferDepth) glGenRenderbuffers(1, &gBufferDepth);

  auto allocColor = [&](GLuint tex, GLint internalFmt, GLenum fmt,
                         GLenum type) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, fbWidth, fbHeight, 0, fmt,
                 type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  allocColor(gPosition, GL_RGBA16F, GL_RGBA, GL_FLOAT);
  allocColor(gNormal, GL_RGBA16F, GL_RGBA, GL_FLOAT);
  allocColor(gAlbedo, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  allocColor(gSpecular, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  allocColor(gEmissive, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);

  glBindRenderbuffer(GL_RENDERBUFFER, gBufferDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbWidth,
                        fbHeight);

  glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         gPosition, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         gNormal, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D,
                         gAlbedo, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D,
                         gSpecular, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D,
                         gEmissive, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, gBufferDepth);
  GLenum drawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                          GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,
                          GL_COLOR_ATTACHMENT4};
  glDrawBuffers(5, drawBuffers);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "gBufferFBO incomplete\n");
  }

  // Lit FBO: deferred lighting writes via MRT (litColor + brightColor);
  // skybox forward-renders to litColor afterward (drawBuffers swapped).
  // Both color attachments are RGBA16F so highlights can exceed 1.0.
  if (!litFBO) glGenFramebuffers(1, &litFBO);
  if (!litColor) glGenTextures(1, &litColor);
  if (!brightColor) glGenTextures(1, &brightColor);
  if (!litDepth) glGenRenderbuffers(1, &litDepth);

  auto allocHDR = [&](GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, fbWidth, fbHeight, 0, GL_RGBA,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  allocHDR(litColor);
  allocHDR(brightColor);

  glBindRenderbuffer(GL_RENDERBUFFER, litDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbWidth,
                        fbHeight);

  glBindFramebuffer(GL_FRAMEBUFFER, litFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         litColor, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         brightColor, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, litDepth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "litFBO incomplete\n");
  }

  // Bloom ping-pong FBOs.
  for (int i = 0; i < 2; ++i) {
    if (!pingFBO[i]) glGenFramebuffers(1, &pingFBO[i]);
    if (!pingColor[i]) glGenTextures(1, &pingColor[i]);
    allocHDR(pingColor[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, pingFBO[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           pingColor[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      fprintf(stderr, "pingFBO[%d] incomplete\n", i);
    }
  }

  // LDR target after tonemap; FXAA reads this.
  if (!ldrFBO) glGenFramebuffers(1, &ldrFBO);
  if (!ldrColor) glGenTextures(1, &ldrColor);
  glBindTexture(GL_TEXTURE_2D, ldrColor);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbWidth, fbHeight, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindFramebuffer(GL_FRAMEBUFFER, ldrFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         ldrColor, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "ldrFBO incomplete\n");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Graphics::initShaderUniforms() {
  // Geometry pass needs the projection matrix; it never changes per frame.
  // The deferred lighting shader's uniforms (lights, shadow maps, viewPos)
  // are all populated per frame in render(), so nothing to do here for it.
  if (gbufferShader && gbufferShader->valid()) {
    gbufferShader->use();
    gbufferShader->setMat4("projection", projection);
  }
}

void Graphics::reloadShaders() {
  struct Reload {
    std::optional<Shader>& slot;
    const char* vert;
    const char* frag;
  };
  Reload reloads[] = {
      {gbufferShader, "shaders/vertex_gbuffer.glsl",
       "shaders/fragment_gbuffer.glsl"},
      {lightingShader, "shaders/vertex_present.glsl",
       "shaders/fragment_lighting_deferred.glsl"},
      {skyboxShader, "shaders/vertex_skybox.glsl",
       "shaders/fragment_skybox.glsl"},
      {presentShader, "shaders/vertex_present.glsl",
       "shaders/fragment_fxaa.glsl"},
      {blurShader, "shaders/vertex_present.glsl",
       "shaders/fragment_blur.glsl"},
      {tonemapShader, "shaders/vertex_present.glsl",
       "shaders/fragment_tonemap.glsl"},
      {shadowDirShader, "shaders/vertex_shadow_dir.glsl",
       "shaders/fragment_shadow_dir.glsl"},
      {debugOverlay, "shaders/vertex_present.glsl",
       "shaders/fragment_debug_overlay.glsl"},
  };
  for (auto& r : reloads) {
    Shader candidate(r.vert, r.frag);
    if (candidate.valid()) {
      r.slot.emplace(std::move(candidate));
      printf("Reloaded: %s + %s\n", r.vert, r.frag);
    } else {
      fprintf(stderr, "Reload failed, keeping previous: %s + %s\n", r.vert,
              r.frag);
    }
  }
  // Geometry-stage reload (point shadow only).
  {
    Shader candidate("shaders/vertex_shadow_point.glsl",
                     "shaders/fragment_shadow_point.glsl",
                     "shaders/geometry_shadow_point.glsl");
    if (candidate.valid()) {
      shadowPointShader.emplace(std::move(candidate));
      printf("Reloaded: vertex_shadow_point + geometry_shadow_point + ...\n");
    } else {
      fprintf(stderr,
              "Reload failed, keeping previous: vertex_shadow_point + ...\n");
    }
  }
  initShaderUniforms();
}

void Graphics::toggleFullscreen() {
  if (!window) return;
  if (fullscreen) {
    glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedW,
                         windowedH, 0);
    fullscreen = false;
  } else {
    glfwGetWindowPos(window, &windowedX, &windowedY);
    glfwGetWindowSize(window, &windowedW, &windowedH);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return;
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                         mode->refreshRate);
    fullscreen = true;
  }
}

void Graphics::cycleDebugChannel() {
  int next = (static_cast<int>(debugChannel) + 1) %
             static_cast<int>(DebugChannel::Count);
  debugChannel = static_cast<DebugChannel>(next);
  const char* name = "?";
  switch (debugChannel) {
    case DebugChannel::Off:
      name = "Off";
      break;
    case DebugChannel::DirShadowMap:
      name = "DirShadowMap";
      break;
    case DebugChannel::Count:
      break;
  }
  printf("Debug overlay: %s\n", name);
}

void Graphics::processDebugKeys() {
  if (!window) return;
  bool f2 = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
  bool f5 = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
  bool f11 = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
  if (f2 && !keyF2Prev) cycleDebugChannel();
  if (f5 && !keyF5Prev) reloadShaders();
  if (f11 && !keyF11Prev) toggleFullscreen();
  keyF2Prev = f2;
  keyF5Prev = f5;
  keyF11Prev = f11;
}

void Graphics::drawDebugOverlay() {
  if (debugChannel == DebugChannel::Off) return;
  if (!debugOverlay || !debugOverlay->valid() || !fullscreenVAO) return;

  GLuint texToShow = 0;
  switch (debugChannel) {
    case DebugChannel::DirShadowMap:
      texToShow = dirShadowMap;
      break;
    case DebugChannel::Off:
    case DebugChannel::Count:
      return;
  }
  if (!texToShow) return;

  // Top-right quarter-screen tile.
  int cornerW = fbWidth / 4;
  int cornerH = fbHeight / 4;
  if (cornerW <= 0 || cornerH <= 0) return;
  int cornerX = fbWidth - cornerW;
  int cornerY = fbHeight - cornerH;

  glDisable(GL_DEPTH_TEST);
  glViewport(cornerX, cornerY, cornerW, cornerH);
  debugOverlay->use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texToShow);
  debugOverlay->setInt("src", 0);
  glBindVertexArray(fullscreenVAO);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glViewport(0, 0, fbWidth, fbHeight);
}

void Graphics::render(ClientGame& game) {
  SIMPLE_PROFILE_SCOPE("Render");
  auto camera = computeCamera(game);
  if (!camera) return;

  // Per-frame light collection. Done up front so the shadow passes and the
  // lighting pass agree on shadow-slot assignments.
  LightUpload lights[kMaxLightingShaderLights];
  int numLights = collectPointLights(game, lights);

  // Directional shadow pass. Light-space matrix tracks the camera so shadows
  // stay sharp around the player.
  lightSpaceMatrix = computeDirectionalLightMatrix(
      camera->position, directionalLightDir(game));
  if (shadowDirShader && shadowDirShader->valid()) {
    SIMPLE_PROFILE_SCOPE("ShadowDir");
    glBindFramebuffer(GL_FRAMEBUFFER, dirShadowFBO);
    glViewport(0, 0, kDirShadowMapSize, kDirShadowMapSize);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    shadowDirShader->use();
    shadowDirShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    renderEntities(*shadowDirShader, game, models, /*forShadowPass=*/true);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // Point-light shadow pass: 24 cubemap-array layers populated in one draw
  // via geometry-shader instancing. Inactive shadow slots (and lights with
  // castsShadow=false) get kill matrices so their layers stay cleared.
  glm::mat4 pointMats[kPointShadowLayers];
  glm::vec3 pointPositions[kMaxPointLights];
  // Initialize to kill matrices for all 24 layers.
  {
    glm::mat4 kill(0.0f);
    kill[3] = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
    for (int i = 0; i < kPointShadowLayers; ++i) pointMats[i] = kill;
    for (int i = 0; i < kMaxPointLights; ++i) pointPositions[i] = glm::vec3(0);
  }
  computePointShadowMatrices(lights, numLights, pointMats, pointPositions);
  if (shadowPointShader && shadowPointShader->valid()) {
    SIMPLE_PROFILE_SCOPE("ShadowPoint");
    glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
    glViewport(0, 0, kPointShadowSize, kPointShadowSize);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    shadowPointShader->use();
    shadowPointShader->setMat4Array(
        "shadowMatrices", kPointShadowLayers,
        glm::value_ptr(pointMats[0]));
    shadowPointShader->setVec3Array("lightPositions", kMaxPointLights,
                                     glm::value_ptr(pointPositions[0]));
    shadowPointShader->setFloat("pointFarPlane", kPointShadowFar);
    renderEntities(*shadowPointShader, game, models, /*forShadowPass=*/true);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // Geometry pass: write the g-buffer.
  {
    SIMPLE_PROFILE_SCOPE("GBuffer");
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
    glViewport(0, 0, fbWidth, fbHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // Clear color = (0,0,0,0); the .a=0 sentinel in gPosition tells the
    // lighting shader "this pixel was never written; skip it" so the skybox
    // can take over.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gbufferShader->use();
    gbufferShader->setMat4("view", camera->view);
    renderEntities(*gbufferShader, game, models);
  }

  // Lighting pass: full-screen triangle reads the g-buffer + shadow maps and
  // writes (litColor, brightColor) via MRT into litFBO. Sky pixels
  // (gPosition.a == 0) are discarded so the skybox can take over.
  {
    SIMPLE_PROFILE_SCOPE("Lighting");
    glBindFramebuffer(GL_FRAMEBUFFER, litFBO);
    GLenum litDrawBufs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, litDrawBufs);
    glViewport(0, 0, fbWidth, fbHeight);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (lightingShader && lightingShader->valid()) {
      lightingShader->use();
      lightingShader->setFloat("bloomThreshold", bloomThreshold);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, gPosition);
      lightingShader->setInt("gPosition", 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, gNormal);
      lightingShader->setInt("gNormal", 1);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, gAlbedo);
      lightingShader->setInt("gAlbedo", 2);
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, gSpecular);
      lightingShader->setInt("gSpecular", 3);
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, gEmissive);
      lightingShader->setInt("gEmissive", 4);
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D, dirShadowMap);
      lightingShader->setInt("dirShadowMap", 5);
      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, pointShadowMaps);
      lightingShader->setInt("pointShadowMaps", 6);
      lightingShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
      lightingShader->setFloat("pointFarPlane", kPointShadowFar);
      lightingShader->setVec3("viewPos", camera->position);
      // Directional light: ECS override beats scene default.
      bool dirSet = false;
      auto dlView = game.renderRegistry.view<shared::DirectionalLight>();
      for (auto ent : dlView) {
        const auto& dl = dlView.get<shared::DirectionalLight>(ent);
        lightingShader->setVec3("dirLight.direction", dl.dirX, dl.dirY,
                                 dl.dirZ);
        lightingShader->setVec3("dirLight.ambient", dl.ambientR, dl.ambientG,
                                 dl.ambientB);
        lightingShader->setVec3("dirLight.diffuse", dl.diffuseR, dl.diffuseG,
                                 dl.diffuseB);
        lightingShader->setVec3("dirLight.specular", dl.specularR,
                                 dl.specularG, dl.specularB);
        dirSet = true;
        break;
      }
      if (!dirSet) {
        if (auto* info = currentScene(game)) {
          lightingShader->setVec3("dirLight.direction", info->dirX, info->dirY,
                                   info->dirZ);
          lightingShader->setVec3("dirLight.ambient", info->ambientR,
                                   info->ambientG, info->ambientB);
          lightingShader->setVec3("dirLight.diffuse", info->diffuseR,
                                   info->diffuseG, info->diffuseB);
          lightingShader->setVec3("dirLight.specular", info->specularR,
                                   info->specularG, info->specularB);
        }
      }
      uploadPointLights(*lightingShader, lights, numLights);
      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
    }
  }

  // Skybox: blit g-buffer depth into litFBO so the skybox depth-tests
  // correctly, then draw forward — but only into litColor, not brightColor
  // (skybox shouldn't bloom).
  {
    SIMPLE_PROFILE_SCOPE("Skybox");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, litFBO);
    glBlitFramebuffer(0, 0, fbWidth, fbHeight, 0, 0, fbWidth, fbHeight,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, litFBO);
    GLenum skyDrawBufs[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, skyDrawBufs);
    glViewport(0, 0, fbWidth, fbHeight);
    glEnable(GL_DEPTH_TEST);
    auto* sceneInfo = currentScene(game);
    if (sceneInfo) {
      std::string skyboxDir = std::string(sceneInfo->skyboxDirectory);
      auto it = skyboxes.find(skyboxDir);
      if (it != skyboxes.end()) {
        drawSkybox(*skyboxShader, it->second, *camera, projection);
      }
    }
  }

  // Bloom blur: ping-pong separable Gaussian, seeded from brightColor.
  GLuint finalBloomColor = brightColor;
  if (blurShader && blurShader->valid() && bloomBlurIterations > 0) {
    SIMPLE_PROFILE_SCOPE("Bloom");
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, fbWidth, fbHeight);
    blurShader->use();
    bool horizontal = true;
    bool firstIter = true;
    for (int i = 0; i < bloomBlurIterations; ++i) {
      int dst = horizontal ? 0 : 1;
      glBindFramebuffer(GL_FRAMEBUFFER, pingFBO[dst]);
      blurShader->setInt("horizontal", horizontal ? 1 : 0);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, firstIter ? brightColor
                                              : pingColor[1 - dst]);
      blurShader->setInt("src", 0);
      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
      finalBloomColor = pingColor[dst];
      horizontal = !horizontal;
      firstIter = false;
    }
  }

  // Tonemap: combine HDR lit + bloom, exposure-tonemap, write LDR.
  {
    SIMPLE_PROFILE_SCOPE("Tonemap");
    glBindFramebuffer(GL_FRAMEBUFFER, ldrFBO);
    glViewport(0, 0, fbWidth, fbHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    if (tonemapShader && tonemapShader->valid()) {
      tonemapShader->use();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, litColor);
      tonemapShader->setInt("hdrColor", 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, finalBloomColor);
      tonemapShader->setInt("bloomColor", 1);
      tonemapShader->setFloat("exposure", exposure);
      tonemapShader->setFloat("bloomStrength", bloomStrength);
      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
    }
  }

  // FXAA present: samples LDR, writes to default framebuffer.
  {
    SIMPLE_PROFILE_SCOPE("Present");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbWidth, fbHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    if (presentShader && presentShader->valid()) {
      presentShader->use();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, ldrColor);
      presentShader->setInt("src", 0);
      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
    }
  }

  drawDebugOverlay();
}

void Graphics::swap() { glfwSwapBuffers(window); }

Graphics::~Graphics() {
  gbufferShader.reset();
  lightingShader.reset();
  skyboxShader.reset();
  presentShader.reset();
  blurShader.reset();
  tonemapShader.reset();
  shadowDirShader.reset();
  shadowPointShader.reset();
  debugOverlay.reset();

  if (fullscreenVAO) {
    glDeleteVertexArrays(1, &fullscreenVAO);
    fullscreenVAO = 0;
  }
  if (gBufferFBO) {
    glDeleteFramebuffers(1, &gBufferFBO);
    gBufferFBO = 0;
  }
  if (gPosition) {
    glDeleteTextures(1, &gPosition);
    gPosition = 0;
  }
  if (gNormal) {
    glDeleteTextures(1, &gNormal);
    gNormal = 0;
  }
  if (gAlbedo) {
    glDeleteTextures(1, &gAlbedo);
    gAlbedo = 0;
  }
  if (gSpecular) {
    glDeleteTextures(1, &gSpecular);
    gSpecular = 0;
  }
  if (gEmissive) {
    glDeleteTextures(1, &gEmissive);
    gEmissive = 0;
  }
  if (gBufferDepth) {
    glDeleteRenderbuffers(1, &gBufferDepth);
    gBufferDepth = 0;
  }
  if (litFBO) {
    glDeleteFramebuffers(1, &litFBO);
    litFBO = 0;
  }
  if (litColor) {
    glDeleteTextures(1, &litColor);
    litColor = 0;
  }
  if (brightColor) {
    glDeleteTextures(1, &brightColor);
    brightColor = 0;
  }
  if (litDepth) {
    glDeleteRenderbuffers(1, &litDepth);
    litDepth = 0;
  }
  for (int i = 0; i < 2; ++i) {
    if (pingFBO[i]) {
      glDeleteFramebuffers(1, &pingFBO[i]);
      pingFBO[i] = 0;
    }
    if (pingColor[i]) {
      glDeleteTextures(1, &pingColor[i]);
      pingColor[i] = 0;
    }
  }
  if (ldrFBO) {
    glDeleteFramebuffers(1, &ldrFBO);
    ldrFBO = 0;
  }
  if (ldrColor) {
    glDeleteTextures(1, &ldrColor);
    ldrColor = 0;
  }
  if (dirShadowFBO) {
    glDeleteFramebuffers(1, &dirShadowFBO);
    dirShadowFBO = 0;
  }
  if (dirShadowMap) {
    glDeleteTextures(1, &dirShadowMap);
    dirShadowMap = 0;
  }
  if (pointShadowFBO) {
    glDeleteFramebuffers(1, &pointShadowFBO);
    pointShadowFBO = 0;
  }
  if (pointShadowMaps) {
    glDeleteTextures(1, &pointShadowMaps);
    pointShadowMaps = 0;
  }

  for (auto& [name, model] : models) {
    delete model;
  }
  models.clear();
  skyboxes.clear();

  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  glfwTerminate();
}
