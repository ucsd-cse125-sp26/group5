// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
// clang-format on

#include "client_graphics.h"

#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shared/assets.h"
#include "shared/components.h"
#include "shared/gpu_profiler.h"
#include "shared/map_format.h"
#include "shared/shader_constants.h"
#include "shared/simple_profiler.h"

// Skybox images are Y-up; the game is Z-up.
static const glm::mat3 kCubemapToGame(1, 0, 0, 0, 0, 1, 0, -1, 0);

// std140 layout — must match CameraBlock in the deferred-lighting,
// vertex_gbuffer, and ssao shaders.
struct alignas(16) CameraUBOData {
  glm::mat4 view;
  glm::mat4 projection;
  glm::mat4 lightSpaceMatrix;
  glm::vec3 viewPos;
  float pointFarPlane;
};
static_assert(sizeof(CameraUBOData) == 208);

static constexpr GLuint kCameraUBOBinding = 0;

static void bindCameraBlock(GLuint prog) {
  GLuint blockIdx = glGetUniformBlockIndex(prog, "CameraBlock");
  if (blockIdx != GL_INVALID_INDEX) {
    glUniformBlockBinding(prog, blockIdx, kCameraUBOBinding);
  }
}

static constexpr int kDirShadowMapSize = 2048;
static constexpr int kPointShadowSize = 1024;
using shared::kMaxLightingShaderLights;
using shared::kMaxPointLights;
using shared::kPointShadowFar;
using shared::kPointShadowLayers;
using shared::kPointShadowNear;

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

static void uploadPointLights(const Shader& shader, const LightUpload* lights,
                              int count) {
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

static void GLAPIENTRY glDebugCallback(GLenum /*source*/, GLenum type,
                                       GLuint /*id*/, GLenum severity,
                                       GLsizei /*length*/, const GLchar* msg,
                                       const void* /*userParam*/) {
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
  const char* sev = severity == GL_DEBUG_SEVERITY_HIGH     ? "HIGH"
                    : severity == GL_DEBUG_SEVERITY_MEDIUM ? "MED"
                    : severity == GL_DEBUG_SEVERITY_LOW    ? "LOW"
                                                           : "?";
  fprintf(stderr, "[GL %s%s] %s\n", sev,
          type == GL_DEBUG_TYPE_ERROR ? " ERROR" : "", msg);
}

// ECS DirectionalLight overrides the scene default; falls back to a fixed
// direction so the shadow pass still runs if neither is configured.
static glm::vec3 directionalLightDir(const ClientGame& game) {
  auto dlView = game.renderRegistry.view<shared::DirectionalLight>();
  for (auto ent : dlView) {
    const auto& dl = dlView.get<shared::DirectionalLight>(ent);
    return {dl.dirX, dl.dirY, dl.dirZ};
  }
  auto sceneView = game.renderRegistry.view<shared::Scene>();
  for (auto ent : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(ent);
    if (auto* info = shared::findScene(scene.name)) {
      return {info->dirX, info->dirY, info->dirZ};
    }
  }
  return {0.3f, 1.0f, -0.4f};
}

// Inactive shadow slots get a "kill" matrix that clips all geometry so the
// cleared depth=1.0 remains (sampled as "not in shadow").
static void computePointShadowMatrices(
    const LightUpload* lights, int count,
    glm::mat4 outMatrices[kPointShadowLayers],
    glm::vec3 outPositions[kMaxPointLights]) {
  static const struct {
    glm::vec3 dir;
    glm::vec3 up;
  } faces[6] = {
      {.dir = {1, 0, 0}, .up = {0, -1, 0}},
      {.dir = {-1, 0, 0}, .up = {0, -1, 0}},
      {.dir = {0, 1, 0}, .up = {0, 0, 1}},
      {.dir = {0, -1, 0}, .up = {0, 0, -1}},
      {.dir = {0, 0, 1}, .up = {0, -1, 0}},
      {.dir = {0, 0, -1}, .up = {0, -1, 0}},
  };

  glm::mat4 kill(0.0f);
  kill[3] = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
  for (int i = 0; i < kPointShadowLayers; ++i) outMatrices[i] = kill;
  for (int i = 0; i < kMaxPointLights; ++i) outPositions[i] = glm::vec3(0.0f);

  glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, kPointShadowNear,
                                    kPointShadowFar);

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

// Camera-following ortho frustum. lightPos is texel-snapped to the light's
// tangent plane so static geometry doesn't shimmer as the camera moves.
static glm::mat4 computeDirectionalLightMatrix(const glm::vec3& cameraPos,
                                               const glm::vec3& lightDir) {
  glm::vec3 dir = glm::normalize(lightDir);
  glm::vec3 up = glm::abs(dir.z) > 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                        : glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec3 right = glm::normalize(glm::cross(dir, up));
  glm::vec3 lightUp = glm::cross(right, dir);
  constexpr float kHalfExtent = 80.0f;
  const float texelWorld = (2.0f * kHalfExtent) / kDirShadowMapSize;
  float u = glm::dot(cameraPos, right);
  float v = glm::dot(cameraPos, lightUp);
  u = std::floor(u / texelWorld) * texelWorld;
  v = std::floor(v / texelWorld) * texelWorld;
  glm::vec3 snapped = right * u + lightUp * v + dir * glm::dot(cameraPos, dir);
  glm::vec3 lightPos = snapped - dir * 120.0f;
  glm::mat4 view = glm::lookAt(lightPos, snapped, up);
  glm::mat4 proj = glm::ortho(-kHalfExtent, kHalfExtent, -kHalfExtent,
                              kHalfExtent, 1.0f, 320.0f);
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
  // Yaw-only so entity pitch/roll doesn't tilt the camera.
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

static const shared::SceneInfo* currentScene(const ClientGame& game) {
  auto sceneView = game.renderRegistry.view<shared::Scene>();
  for (auto ent : sceneView) {
    auto& scene = sceneView.get<shared::Scene>(ent);
    auto* info = shared::findScene(scene.name);
    if (info) return info;
  }
  return nullptr;
}

// ECS DirectionalLight overrides the scene default. First entity wins.
static void uploadDirectionalLight(const Shader& shader,
                                   const ClientGame& game) {
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
      // Light-marker meshes sit at the light's origin and would shadow
      // their own light if rendered into the shadow map.
      if (game.renderRegistry
              .any_of<shared::PointLight, shared::DirectionalLight>(ent))
        continue;
    } else {
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
  // Debug context — KHR_debug is core in 4.3 but widely available as an
  // extension on 4.1 (which we target for macOS support). We install the
  // callback below only if the loader picked up the function pointer.
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

  window = glfwCreateWindow(width, height, "Hello World", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (glfwRawMouseMotionSupported())
    glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

  glfwGetWindowPos(window, &windowedX, &windowedY);
  glfwGetWindowSize(window, &windowedW, &windowedH);
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

  int version = gladLoadGL(glfwGetProcAddress);
  printf("GL %d.%d\n", GLAD_VERSION_MAJOR(version),
         GLAD_VERSION_MINOR(version));

  if (glDebugMessageCallback) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugCallback, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr,
                          GL_TRUE);
  }

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
  ssaoShader.emplace("shaders/vertex_present.glsl",
                     "shaders/fragment_ssao.glsl");
  ssaoBlurShader.emplace("shaders/vertex_present.glsl",
                         "shaders/fragment_ssao_blur.glsl");
  shadowDirShader.emplace("shaders/vertex_shadow_dir.glsl",
                          "shaders/fragment_shadow_dir.glsl");
  shadowPointShader.emplace("shaders/vertex_shadow_point.glsl",
                            "shaders/fragment_shadow_point.glsl");
  debugOverlay.emplace("shaders/vertex_present.glsl",
                       "shaders/fragment_debug_overlay.glsl");

  // Shader ctor failures are silent at runtime so F5 hot-reload can keep
  // the previous program — at startup there is no previous program, so
  // fail fast instead of starting with a black window.
  const std::optional<Shader>* required[] = {
      &gbufferShader,   &lightingShader,    &skyboxShader, &presentShader,
      &blurShader,      &tonemapShader,     &ssaoShader,   &ssaoBlurShader,
      &shadowDirShader, &shadowPointShader, &debugOverlay,
  };
  for (const auto* s : required) {
    if (!*s || !(*s)->valid()) {
      fprintf(stderr, "Graphics::load: required shader failed to compile\n");
      return false;
    }
  }

  // Directional shadow map. Size is fixed; resizeBuffers leaves it alone.
  glGenFramebuffers(1, &dirShadowFBO);
  glGenTextures(1, &dirShadowMap);
  glBindTexture(GL_TEXTURE_2D, dirShadowMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kDirShadowMapSize,
               kDirShadowMapSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  // LINEAR + COMPARE_REF_TO_TEXTURE → 2×2 hardware PCF per tap.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                  GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  // Outside the frustum returns visibility=1.0 (fully lit).
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

  // Point-light cubemap array: 24 layer-faces (4 cubes × 6 faces) bound as
  // a layered attachment so the geometry shader can route via gl_Layer.
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
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_MODE,
                  GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_FUNC,
                  GL_LEQUAL);
  glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
  glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, pointShadowMaps, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "pointShadowFBO incomplete\n");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Empty VAO for fullscreen-triangle draws; positions synthesized from
  // gl_VertexID in vertex_present.glsl.
  glGenVertexArrays(1, &fullscreenVAO);

  glGenBuffers(1, &cameraUBO);
  glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraUBOData), nullptr,
               GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_UNIFORM_BUFFER, kCameraUBOBinding, cameraUBO);
  for (auto* s : {&*gbufferShader, &*lightingShader, &*ssaoShader}) {
    bindCameraBlock(s->id());
  }

  // SSAO: 64-sample hemisphere kernel biased towards the origin so closer
  // samples weight more heavily.
  {
    std::default_random_engine gen(0xc4b1u);
    std::uniform_real_distribution<float> rand01(0.0f, 1.0f);
    auto lerp = [](float a, float b, float t) { return a + t * (b - a); };
    ssaoKernel.clear();
    ssaoKernel.reserve(64);
    for (int i = 0; i < 64; ++i) {
      glm::vec3 sample(rand01(gen) * 2.0f - 1.0f, rand01(gen) * 2.0f - 1.0f,
                       rand01(gen));
      sample = glm::normalize(sample);
      sample *= rand01(gen);
      float scale = static_cast<float>(i) / 64.0f;
      sample *= lerp(0.1f, 1.0f, scale * scale);
      ssaoKernel.push_back(sample);
    }
    // 4×4 noise tile of tangent-space rotation vectors; z=0 so rotation is
    // around the surface normal.
    std::vector<glm::vec3> noise(16);
    for (int i = 0; i < 16; ++i) {
      noise[i] =
          glm::vec3(rand01(gen) * 2.0f - 1.0f, rand01(gen) * 2.0f - 1.0f, 0.0f);
    }
    glGenTextures(1, &ssaoNoiseTex);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT,
                 noise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }

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

  // Per-node sub-models keyed to match RenderInfo.modelName from map_loader.
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
      static_cast<float>(fbWidth) / static_cast<float>(fbHeight), 0.1f, 500.0f);

  // gPosition: full FP32 — half-float quantizes distant world-space neighbors
  // to the same value, breaking SSAO. Normal fits in RGBA16F; albedo,
  // specular, and emissive fit in RGBA8.
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
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, fbWidth, fbHeight, 0, fmt, type,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };
  // RGBA32F: world position needs full FP32 precision so SSAO/shadows on
  // distant geometry don't quantize neighbors to the same value.
  allocColor(gPosition, GL_RGBA32F, GL_RGBA, GL_FLOAT);
  allocColor(gNormal, GL_RGBA16F, GL_RGBA, GL_FLOAT);
  allocColor(gAlbedo, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  allocColor(gSpecular, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
  allocColor(gEmissive, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);

  glBindRenderbuffer(GL_RENDERBUFFER, gBufferDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, fbWidth,
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

  // litColor + brightColor: HDR MRT for deferred lighting; skybox writes
  // litColor only on the second pass.
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
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, fbWidth,
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

  auto allocSsao = [&](GLuint& fbo, GLuint& tex) {
    if (!fbo) glGenFramebuffers(1, &fbo);
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, fbWidth, fbHeight, 0, GL_RED,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      fprintf(stderr, "ssao FBO incomplete\n");
    }
    // Clear to 1.0 (no occlusion) so the lighting pass reads valid values
    // before SSAO has run.
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  };
  allocSsao(ssaoFBO, ssaoColor);
  allocSsao(ssaoBlurFBO, ssaoBlurColor);

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
  // Re-bind CameraBlock after hot-reload produces a fresh program object.
  for (auto* s : {&gbufferShader, &lightingShader, &ssaoShader}) {
    if (*s && (*s)->valid()) bindCameraBlock((*s)->id());
  }
}

void Graphics::reloadShaders() {
  struct Reload {
    std::optional<Shader>& slot;
    const char* vert;
    const char* frag;
    const char* geom;  // "" = no geometry stage
  };
  Reload reloads[] = {
      {.slot = gbufferShader,
       .vert = "shaders/vertex_gbuffer.glsl",
       .frag = "shaders/fragment_gbuffer.glsl",
       .geom = ""},
      {.slot = lightingShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_lighting_deferred.glsl",
       .geom = ""},
      {.slot = skyboxShader,
       .vert = "shaders/vertex_skybox.glsl",
       .frag = "shaders/fragment_skybox.glsl",
       .geom = ""},
      {.slot = presentShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_fxaa.glsl",
       .geom = ""},
      {.slot = blurShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_blur.glsl",
       .geom = ""},
      {.slot = tonemapShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_tonemap.glsl",
       .geom = ""},
      {.slot = ssaoShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_ssao.glsl",
       .geom = ""},
      {.slot = ssaoBlurShader,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_ssao_blur.glsl",
       .geom = ""},
      {.slot = shadowDirShader,
       .vert = "shaders/vertex_shadow_dir.glsl",
       .frag = "shaders/fragment_shadow_dir.glsl",
       .geom = ""},
      {.slot = shadowPointShader,
       .vert = "shaders/vertex_shadow_point.glsl",
       .frag = "shaders/fragment_shadow_point.glsl",
       .geom = ""},
      {.slot = debugOverlay,
       .vert = "shaders/vertex_present.glsl",
       .frag = "shaders/fragment_debug_overlay.glsl",
       .geom = ""},
  };
  for (auto& r : reloads) {
    Shader candidate = (r.geom && *r.geom) ? Shader(r.vert, r.frag, r.geom)
                                           : Shader(r.vert, r.frag);
    if (candidate.valid()) {
      r.slot.emplace(std::move(candidate));
      printf("Reloaded: %s + %s%s%s\n", r.vert, r.frag,
             (r.geom && *r.geom) ? " + " : "", r.geom ? r.geom : "");
    } else {
      fprintf(stderr, "Reload failed, keeping previous: %s + %s\n", r.vert,
              r.frag);
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
  // Shadow textures use COMPARE_REF_TO_TEXTURE for hardware PCF, but the
  // overlay samples as plain sampler2D — temporarily flip compare off.
  bool isDirShadow = (debugChannel == DebugChannel::DirShadowMap);
  if (isDirShadow) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
  }
  debugOverlay->setInt("src", 0);
  glBindVertexArray(fullscreenVAO);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  if (isDirShadow) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                    GL_COMPARE_REF_TO_TEXTURE);
  }
  glViewport(0, 0, fbWidth, fbHeight);
}

void Graphics::render(ClientGame& game) {
  SIMPLE_PROFILE_SCOPE("Render");
  GPU_PROFILE_SCOPE("Render");
  auto camera = computeCamera(game);
  if (!camera) return;

  // Collect lights up front so shadow passes and the lighting pass agree
  // on shadow-slot assignments.
  LightUpload lights[kMaxLightingShaderLights];
  int numLights = collectPointLights(game, lights);

  lightSpaceMatrix = computeDirectionalLightMatrix(camera->position,
                                                   directionalLightDir(game));

  {
    CameraUBOData ubo{};
    ubo.view = camera->view;
    ubo.projection = projection;
    ubo.lightSpaceMatrix = lightSpaceMatrix;
    ubo.viewPos = camera->position;
    ubo.pointFarPlane = kPointShadowFar;
    glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ubo), &ubo);
  }
  if (shadowDirShader && shadowDirShader->valid()) {
    SIMPLE_PROFILE_SCOPE("ShadowDir");
    GPU_PROFILE_SCOPE("ShadowDir");
    glBindFramebuffer(GL_FRAMEBUFFER, dirShadowFBO);
    glViewport(0, 0, kDirShadowMapSize, kDirShadowMapSize);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    // Polygon offset only; front-face culling here causes peter-panning
    // on the single-sided floor/walls.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    shadowDirShader->use();
    shadowDirShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    renderEntities(*shadowDirShader, game, models, /*forShadowPass=*/true);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // Point shadows: one depth pass per active light × 6 cube faces. Each
  // iteration rebinds the FBO depth attachment to a single cubemap-array
  // layer. Avoids the geometry-shader expansion that dominated GPU time
  // when this was a single layered draw.
  glm::mat4 pointMats[kPointShadowLayers];
  glm::vec3 pointPositions[kMaxPointLights];
  computePointShadowMatrices(lights, numLights, pointMats, pointPositions);
  if (shadowPointShader && shadowPointShader->valid() && numLights > 0) {
    SIMPLE_PROFILE_SCOPE("ShadowPoint");
    GPU_PROFILE_SCOPE("ShadowPoint");
    glBindFramebuffer(GL_FRAMEBUFFER, pointShadowFBO);
    glViewport(0, 0, kPointShadowSize, kPointShadowSize);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    shadowPointShader->use();
    shadowPointShader->setFloat("pointFarPlane", kPointShadowFar);
    for (int i = 0; i < numLights; ++i) {
      int slot = lights[i].shadowIdx;
      if (slot < 0 || slot >= kMaxPointLights) continue;
      shadowPointShader->setVec3("lightPos", lights[i].position);
      for (int f = 0; f < 6; ++f) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  pointShadowMaps, 0, slot * 6 + f);
        glClear(GL_DEPTH_BUFFER_BIT);
        shadowPointShader->setMat4("lightSpaceMatrix",
                                   pointMats[slot * 6 + f]);
        renderEntities(*shadowPointShader, game, models,
                       /*forShadowPass=*/true);
      }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  {
    SIMPLE_PROFILE_SCOPE("GBuffer");
    GPU_PROFILE_SCOPE("GBuffer");
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
    glViewport(0, 0, fbWidth, fbHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    // gPosition.a = 0 marks "no geometry written" so the skybox can take
    // those pixels.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gbufferShader->use();
    renderEntities(*gbufferShader, game, models);
  }

  if (ssaoShader && ssaoShader->valid()) {
    SIMPLE_PROFILE_SCOPE("SSAO");
    GPU_PROFILE_SCOPE("SSAO");
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
    glViewport(0, 0, fbWidth, fbHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoShader->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gPosition);
    ssaoShader->setInt("gPosition", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormal);
    ssaoShader->setInt("gNormal", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex);
    ssaoShader->setInt("texNoise", 2);
    ssaoShader->setVec3Array("samples", static_cast<int>(ssaoKernel.size()),
                             glm::value_ptr(ssaoKernel[0]));
    ssaoShader->setInt("kernelSize", ssaoKernelSize);
    ssaoShader->setFloat("radius", ssaoRadius);
    ssaoShader->setFloat("bias", ssaoBias);
    ssaoShader->setVec2("noiseScale", static_cast<float>(fbWidth) / 4.0f,
                        static_cast<float>(fbHeight) / 4.0f);
    glBindVertexArray(fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
  }

  if (ssaoBlurShader && ssaoBlurShader->valid()) {
    SIMPLE_PROFILE_SCOPE("SSAOBlur");
    GPU_PROFILE_SCOPE("SSAOBlur");
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO);
    glViewport(0, 0, fbWidth, fbHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    ssaoBlurShader->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssaoColor);
    ssaoBlurShader->setInt("src", 0);
    glBindVertexArray(fullscreenVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
  }

  {
    SIMPLE_PROFILE_SCOPE("Lighting");
    GPU_PROFILE_SCOPE("Lighting");
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
      glBindTexture(GL_TEXTURE_2D, ssaoBlurColor);
      lightingShader->setInt("ssao", 5);
      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_2D, dirShadowMap);
      lightingShader->setInt("dirShadowMap", 6);
      glActiveTexture(GL_TEXTURE7);
      glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, pointShadowMaps);
      lightingShader->setInt("pointShadowMaps", 7);
      uploadDirectionalLight(*lightingShader, game);
      uploadPointLights(*lightingShader, lights, numLights);
      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
    }
  }

  // Blit g-buffer depth into litFBO so the skybox depth-tests against
  // scene geometry. Skybox writes litColor only (no bloom).
  {
    SIMPLE_PROFILE_SCOPE("Skybox");
    GPU_PROFILE_SCOPE("Skybox");
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

  GLuint finalBloomColor = brightColor;
  if (blurShader && blurShader->valid() && bloomBlurIterations > 0) {
    SIMPLE_PROFILE_SCOPE("Bloom");
    GPU_PROFILE_SCOPE("Bloom");
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
      glBindTexture(GL_TEXTURE_2D,
                    firstIter ? brightColor : pingColor[1 - dst]);
      blurShader->setInt("src", 0);
      glBindVertexArray(fullscreenVAO);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glBindVertexArray(0);
      finalBloomColor = pingColor[dst];
      horizontal = !horizontal;
      firstIter = false;
    }
  }

  {
    SIMPLE_PROFILE_SCOPE("Tonemap");
    GPU_PROFILE_SCOPE("Tonemap");
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

  {
    SIMPLE_PROFILE_SCOPE("Present");
    GPU_PROFILE_SCOPE("Present");
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
  // Renderer isn't recycled at runtime, so per-handle glDelete* would be
  // dead code. Tearing down GLFW is enough.
  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  glfwTerminate();
}
