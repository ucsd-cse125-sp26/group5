#include <glad/gl.h>

#include <string>
#include <vector>

#include "glm/ext/matrix_float4x4.hpp"

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
};

struct MaterialSlot {
  glm::vec3 constant = glm::vec3(1.0f);
  GLuint texture = 0;
};

struct Material {
  MaterialSlot ambient;
  MaterialSlot diffuse;
  MaterialSlot specular;
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
  GLuint test;
};

Model* loadModel(std::string filename);
