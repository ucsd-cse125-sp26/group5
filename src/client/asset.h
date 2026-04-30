#pragma once

#include <glad/gl.h>

#include <string>
#include <vector>

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/quaternion_float.hpp"
#include "shared/assets.h"

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texture_coordinates;
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
  float shininess = 32.0f;
};

struct Mesh {
  std::vector<Vertex> vertices;
  unsigned int materialIndex;
  GLuint vao, vbo, ebo, index_count;
};

struct Model {
  std::vector<Mesh> meshes;
  std::vector<Material> materials;
  std::vector<std::pair<unsigned int, glm::mat4>> mesh_instances;
  glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
  GLuint test;
};

struct Skybox {
  GLuint vao;
  GLuint cubemapTexture;
};

class Shader;

Model* loadModel(const std::string& filename);
Model* makeCubeModel(const shared::CubeSpec& spec);
Skybox loadSkybox(const std::string& directory);
void Draw(const Shader& shader, const Mesh& mesh, const Material& material);
void Draw(const Shader& shader, const Model& model, const glm::mat4& transform);

// Loads a glTF/glb map and emits one Model per mesh-bearing node, keyed by
// shared::MAP_MODEL_PREFIX + nodeName. Each Model has identity local mesh
// transforms — the node's world transform is on the server-spawned entity's
// Position + RenderInfo.scale, not baked into the geometry.
std::vector<std::pair<std::string, Model*>> loadMapModels(
    const std::string& filename);
