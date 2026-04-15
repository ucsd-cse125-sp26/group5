#include "asset.h"

#include <iostream>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "client/util.h"
#include "glm/gtc/type_ptr.hpp"

static inline glm::vec3 vec3_cast(const aiVector3D& v) {
  return glm::vec3(v.x, v.y, v.z);
}
static inline glm::vec2 vec2_cast(const aiVector3D& v) {
  return glm::vec2(v.x, v.y);
}  // it's aiVector3D because assimp's texture coordinates use that
static inline glm::quat quat_cast(const aiQuaternion& q) {
  return glm::quat(q.w, q.x, q.y, q.z);
}
static inline glm::mat4 mat4_cast(const aiMatrix4x4& m) {
  return glm::transpose(glm::make_mat4(&m.a1));
}

Model* loadModel(std::string filename) {
  auto base = exeDir();
  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(
      base / filename, aiProcess_Triangulate | aiProcess_FlipUVs);
  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
      !scene->mRootNode) {
    std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
    return NULL;
  }

  Model* model = new Model();
  if (!model) return nullptr;

  std::vector<Mesh> meshes;
  for (int i = 0; i < scene->mNumMeshes; i++) {
    std::vector<Vertex> vertices;
    auto mesh = scene->mMeshes[i];
    for (int j = 0; j < mesh->mNumVertices; j++) {
      Vertex vertex;
      vertex.position = vec3_cast(mesh->mVertices[j]);
      vertex.normal = vec3_cast(mesh->mNormals[j]);
      vertices.push_back(vertex);
    }
    meshes.emplace_back(vertices, 0, 0, 0, 0, 0);
  }
  return model;
}
