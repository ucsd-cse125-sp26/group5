#pragma once

#include <glad/gl.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "shared/assets.h"
#include "shared/mesh_loader.h"
#include "shared/puzzles/tangram/puzzle_data.h"

// Skinning caps. Vertex slot count must match MAX_BONE_INFLUENCE, and the
// vertex shaders' uniform array length must match MAX_BONES.
inline constexpr int MAX_BONE_INFLUENCE = 4;
inline constexpr int MAX_BONES = 100;

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texture_coordinates;
  glm::vec3 tangent;
  glm::vec3 bitangent;
  // -1 means "unused slot"; the shader skips any boneIDs[i] < 0. Default
  // value-init to 0 is also harmless because cubes/etc. don't enable
  // skinning, so the shader never reads these.
  int boneIDs[MAX_BONE_INFLUENCE] = {-1, -1, -1, -1};
  float weights[MAX_BONE_INFLUENCE] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct BoneInfo {
  int id = -1;
  glm::mat4 offset{1.0f};
};

struct MaterialSlot {
  glm::vec3 constant = glm::vec3(1.0f);
  GLuint texture = 0;
};

struct Material {
  MaterialSlot ambient;
  MaterialSlot diffuse;
  MaterialSlot specular;
  MaterialSlot emissive;
  MaterialSlot normal;
  float shininess = 32.0f;
};

struct Mesh {
  std::vector<Vertex> vertices;
  unsigned int materialIndex;
  GLuint vao, vbo, ebo, index_count;
};

// One linear-RGB sample drawn at a triangle's UV (centroid or vertex),
// weighted by the triangle's mesh-local surface area / 4. The k-means
// builder uses these so large triangles influence the model's palette
// proportionally more than small ones.
struct DiffuseSample {
  glm::vec3 color;
  float weight;
};

struct Model {
  std::vector<Mesh> meshes;
  std::vector<Material> materials;
  std::vector<std::pair<unsigned int, glm::mat4>> mesh_instances;
  glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
  // Retained so the palette can be rebuilt at runtime when the user changes
  // paletteQuantizeColors. Populated by loadModel / loadMapModels.
  std::vector<DiffuseSample> diffuseSamples;
  // Current k-means centroids; empty when palette quantization is disabled.
  std::vector<glm::vec3> palette;

  // Skeleton — empty for non-skinned models. Populated by loadModel only.
  bool skinned = false;
  std::unordered_map<std::string, BoneInfo> boneInfoMap;
  int boneCount = 0;
  // Kept alive so AnimationLibrary can re-read clip channels from aiScene
  // without re-parsing the file. shared_ptr because the library borrows it.
  std::shared_ptr<shared::ParsedModel> parsed;
  // First-class hook for a "look pitch" override — case-insensitive scan
  // over the skeleton picks the first node whose name contains "neck", with
  // "head" as a fallback. Empty when neither exists.
  std::string neckBoneName;

  // Local-space bounding sphere covering all mesh_instances' transformed
  // vertices. Computed once at model construction; used by shadow culling.
  // Radius == 0 means "bounds unknown / empty mesh" — callers should treat
  // that as "do not cull".
  glm::vec3 localBoundsCenter{0.0f};
  float localBoundsRadius = 0.0f;
};

struct Skybox {
  GLuint vao;
  GLuint cubemapTexture;
  // Stride-sampled cubemap face pixels, kept around so the palette can be
  // rebuilt at runtime when the user changes skyboxPaletteColors. Linear-RGB
  // colors with unit weight; sampling is uniform across face pixels.
  std::vector<DiffuseSample> diffuseSamples;
  // Current k-means centroids; empty when skybox palette quantization is off.
  std::vector<glm::vec3> palette;
};

class Shader;

Model* loadModel(const std::string& filename);
Model* makeCubeModel(const shared::CubeSpec& spec);
// Procedural UV sphere (lat/long grid) using the spec's emissive color as a
// solid emissive surface. Diffuse uses the first palette entry.
// emissiveBoost multiplies the material's emissive constant — bump above 1.0
// to push the surface past the bloom threshold (used by the moon).
Model* makeSphereModel(const shared::CubeSpec& spec, int rings = 16,
                       int segments = 28, float emissiveBoost = 1.0f);
// Flat tangram piece mesh (triangle / square / parallelogram) in the X/Y plane,
// Z up.
Model* makeTangramPieceModel(const shared::tangram_puzzle::PieceDef& def);
// Grey silhouette for non–slot-2 players when color isolation is on.
Model* makeTangramPieceMuteModel(const shared::tangram_puzzle::PieceDef& def);
Model* makeTangramGhostSlotModel(const shared::tangram_puzzle::PieceDef& def);
// Ghost outline tinted like the playable piece (for slot 3 placement guide).
Model* makeTangramColoredGhostSlotModel(
    const shared::tangram_puzzle::PieceDef& def);
// Player join order 1–4: same rainbow cube with digit 1–4 on the top face.
Model* makePlayerSlotCubeModel(const shared::CubeSpec& spec, uint8_t slot);
Skybox loadSkybox(const std::string& directory);
void Draw(const Shader& shader, const Mesh& mesh, const Material& material);
void Draw(const Shader& shader, const Model& model, const glm::mat4& transform);
// Skinned variant. `bones` points to `count` mat4 entries; pass count==0 to
// fall back to the non-skinned path (sets useSkinning=0 in the shader).
void Draw(const Shader& shader, const Model& model, const glm::mat4& transform,
          const glm::mat4* bones, int count);

// One Model per mesh-bearing glTF node, keyed by MAP_MODEL_PREFIX + name.
// Local mesh transforms are identity; node world transform lives on the
// server-spawned entity's Position + RenderInfo.scale.
std::vector<std::pair<std::string, Model*>> loadMapModels(
    const std::string& filename);

// Run weighted k-means on model.diffuseSamples and store the resulting
// centroids in model.palette. colors <= 0, an empty sample buffer, or a
// total weight of zero leave model.palette empty.
void buildModelPalette(Model& model, int colors);
// Same algorithm against the cubemap face samples retained on the Skybox.
void buildSkyboxPalette(Skybox& skybox, int colors);
