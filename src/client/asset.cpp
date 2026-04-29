#include "asset.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <iostream>
#include <utility>
#include <vector>

#include "assimp/Importer.hpp"
#include "assimp/material.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "client/shaders.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shared/util.h"

static inline glm::vec3 vec3_cast(const aiVector3D& v) {
  return {v.x, v.y, v.z};
}
static inline glm::vec2 vec2_cast(const aiVector3D& v) {
  return {v.x, v.y};
}  // it's aiVector3D because assimp's texture coordinates use that
static inline glm::quat quat_cast(const aiQuaternion& q) {
  return {q.w, q.x, q.y, q.z};
}
static inline glm::mat4 mat4_cast(const aiMatrix4x4& m) {
  return glm::transpose(glm::make_mat4(&m.a1));
}

MaterialSlot loadMaterial(const aiMaterial* mat, aiTextureType type,
                          const aiScene* scene, const std::string& base) {
  if (mat->GetTextureCount(type) > 0) {
    aiString path;
    mat->GetTexture(type, 0, &path);

    int w, h, channels;
    int pixel_order;
    uint8_t* pixels;
    // Handle embedded textures (path starts with '*')
    if (auto embedded = scene->GetEmbeddedTexture(path.C_Str())) {
      if (embedded->mHeight == 0) {
        pixels =
            stbi_load_from_memory(reinterpret_cast<uint8_t*>(embedded->pcData),
                                  embedded->mWidth, &w, &h, &channels, 4);
        pixel_order = GL_RGBA;
      } else {
        w = embedded->mWidth;
        h = embedded->mHeight;
        pixel_order = GL_BGRA;
        pixels = reinterpret_cast<uint8_t*>(embedded->pcData);
      }
    } else {
      // Otherwise load from disk
      std::string fullPath = base + "/" + path.C_Str();
      pixels = stbi_load(fullPath.c_str(), &w, &h, &channels, 4);
      pixel_order = GL_RGBA;
    }

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, pixel_order,
                 GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    if (pixel_order == GL_RGBA) {
      stbi_image_free(pixels);
    }
    return MaterialSlot{.constant = glm::vec3(1.0f), .texture = id};
  }

  aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);  // default white
  switch (type) {
    case aiTextureType_DIFFUSE:
      mat->Get(AI_MATKEY_COLOR_DIFFUSE, color);
      break;
    case aiTextureType_SPECULAR:
      mat->Get(AI_MATKEY_COLOR_SPECULAR, color);
      break;
    case aiTextureType_EMISSIVE:
      mat->Get(AI_MATKEY_COLOR_EMISSIVE, color);
      break;
    case aiTextureType_AMBIENT:
      mat->Get(AI_MATKEY_COLOR_AMBIENT, color);
      break;
    default:
      break;
  }
  uint8_t pixel[4] = {
      static_cast<uint8_t>(color.r * 255), static_cast<uint8_t>(color.g * 255),
      static_cast<uint8_t>(color.b * 255), static_cast<uint8_t>(color.a * 255)};

  GLuint id;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixel);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  return MaterialSlot{.constant = glm::vec3(1.0f), .texture = id};
}

Model* loadModel(const std::string& filename) {
  // MinGW's std::filesystem::path::string_type is wstring, so the implicit
  // conversions that work on Linux don't work here. Convert explicitly.
  const std::string baseStr = exeDir().string();
  const std::string fullPath = (exeDir() / filename).string();
  Assimp::Importer importer;
  const aiScene* scene =
      importer.ReadFile(fullPath, aiProcess_Triangulate | aiProcess_FlipUVs);
  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
      !scene->mRootNode) {
    std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << '\n';
    return nullptr;
  }

  auto* model = new Model();
  if (!model) return nullptr;

  for (int i = 0; i < scene->mNumMaterials; i++) {
    aiMaterial* aimat = scene->mMaterials[i];
    Material result;
    result.ambient = loadMaterial(aimat, aiTextureType_AMBIENT, scene, baseStr);
    result.diffuse = loadMaterial(aimat, aiTextureType_DIFFUSE, scene, baseStr);
    result.specular =
        loadMaterial(aimat, aiTextureType_SPECULAR, scene, baseStr);
    result.emissive =
        loadMaterial(aimat, aiTextureType_EMISSIVE, scene, baseStr);
    aimat->Get(AI_MATKEY_SHININESS, result.shininess);
    model->materials.push_back(result);
  }

  for (int i = 0; i < scene->mNumMeshes; i++) {
    auto mesh = scene->mMeshes[i];
    std::vector<Vertex> vertices;
    vertices.reserve(mesh->mNumVertices);
    for (int j = 0; j < mesh->mNumVertices; j++) {
      Vertex vertex;
      vertex.position = vec3_cast(mesh->mVertices[j]);
      vertex.normal = vec3_cast(mesh->mNormals[j]);
      if (mesh->mTextureCoords[0]) {
        vertex.texture_coordinates = vec2_cast(mesh->mTextureCoords[0][j]);
      } else {
        vertex.texture_coordinates = glm::vec2(0.0f, 0.0f);
      }
      vertices.push_back(vertex);
    }

    std::vector<GLuint> indices;
    indices.reserve(mesh->mNumFaces * 3);
    for (int j = 0; j < mesh->mNumFaces; j++) {
      const aiFace& face = mesh->mFaces[j];
      for (int k = 0; k < face.mNumIndices; k++) {
        indices.push_back(face.mIndices[k]);
      }
    }

    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint),
                 indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, texture_coordinates));

    glBindVertexArray(0);

    Mesh m;
    m.vertices = std::move(vertices);
    m.materialIndex = mesh->mMaterialIndex;
    m.vao = vao;
    m.vbo = vbo;
    m.ebo = ebo;
    m.index_count = static_cast<GLuint>(indices.size());
    model->meshes.push_back(std::move(m));
  }

  std::vector<std::pair<const aiNode*, glm::mat4>> stack;
  stack.emplace_back(scene->mRootNode, glm::mat4(1.0f));
  while (!stack.empty()) {
    auto [node, parent] = stack.back();
    stack.pop_back();
    glm::mat4 transform = parent * mat4_cast(node->mTransformation);
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
      model->mesh_instances.emplace_back(node->mMeshes[i], transform);
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
      stack.emplace_back(node->mChildren[i], transform);
    }
  }

  return model;
}

static GLuint makeSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  uint8_t pixel[4] = {r, g, b, a};
  GLuint id;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixel);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return id;
}

Model* makeCubeModel(const shared::CubeSpec& spec) {
  struct Face {
    glm::vec3 normal;
    glm::vec3 corners[4];
  };
  // Face order: back, front, left, right, bottom, top (matches the palette
  // below; each face samples one texel of a 6x1 diffuse texture).
  const Face faces[6] = {
      {.normal = {0, 0, -1},
       .corners = {{-0.5f, -0.5f, -0.5f},
                   {0.5f, -0.5f, -0.5f},
                   {0.5f, 0.5f, -0.5f},
                   {-0.5f, 0.5f, -0.5f}}},
      {.normal = {0, 0, 1},
       .corners = {{-0.5f, -0.5f, 0.5f},
                   {0.5f, -0.5f, 0.5f},
                   {0.5f, 0.5f, 0.5f},
                   {-0.5f, 0.5f, 0.5f}}},
      {.normal = {-1, 0, 0},
       .corners = {{-0.5f, -0.5f, -0.5f},
                   {-0.5f, 0.5f, -0.5f},
                   {-0.5f, 0.5f, 0.5f},
                   {-0.5f, -0.5f, 0.5f}}},
      {.normal = {1, 0, 0},
       .corners = {{0.5f, -0.5f, -0.5f},
                   {0.5f, 0.5f, -0.5f},
                   {0.5f, 0.5f, 0.5f},
                   {0.5f, -0.5f, 0.5f}}},
      {.normal = {0, -1, 0},
       .corners = {{-0.5f, -0.5f, -0.5f},
                   {0.5f, -0.5f, -0.5f},
                   {0.5f, -0.5f, 0.5f},
                   {-0.5f, -0.5f, 0.5f}}},
      {.normal = {0, 1, 0},
       .corners = {{-0.5f, 0.5f, -0.5f},
                   {0.5f, 0.5f, -0.5f},
                   {0.5f, 0.5f, 0.5f},
                   {-0.5f, 0.5f, 0.5f}}},
  };

  std::vector<Vertex> vertices;
  vertices.reserve(24);
  std::vector<GLuint> indices;
  indices.reserve(36);
  for (int f = 0; f < 6; f++) {
    auto base = static_cast<GLuint>(vertices.size());
    float u = (f + 0.5f) / 6.0f;
    glm::vec2 uv(u, 0.5f);
    for (auto corner : faces[f].corners) {
      vertices.push_back({.position = corner,
                          .normal = faces[f].normal,
                          .texture_coordinates = uv});
    }
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
  }

  GLuint vao, vbo, ebo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);

  glBindVertexArray(vao);

  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint),
               indices.data(), GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void*)offsetof(Vertex, position));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void*)offsetof(Vertex, normal));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void*)offsetof(Vertex, texture_coordinates));

  glBindVertexArray(0);

  // 6x1 diffuse palette, one texel per face (matches `faces` order above).
  GLuint diffuseTex;
  glGenTextures(1, &diffuseTex);
  glBindTexture(GL_TEXTURE_2D, diffuseTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 6, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               spec.palette);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  auto* model = new Model();

  Material material;
  material.ambient = {.constant = glm::vec3(1.0f), .texture = diffuseTex};
  material.diffuse = {.constant = glm::vec3(1.0f), .texture = diffuseTex};
  material.specular = {.constant = glm::vec3(1.0f),
                       .texture = makeSolidTexture(255, 255, 255, 255)};
  material.emissive = {
      .constant = glm::vec3(1.0f),
      .texture = makeSolidTexture(spec.emissive[0], spec.emissive[1],
                                  spec.emissive[2], spec.emissive[3])};
  material.shininess = 32.0f;
  model->materials.push_back(material);

  Mesh mesh;
  mesh.vertices = std::move(vertices);
  mesh.materialIndex = 0;
  mesh.vao = vao;
  mesh.vbo = vbo;
  mesh.ebo = ebo;
  mesh.index_count = static_cast<GLuint>(indices.size());
  model->meshes.push_back(std::move(mesh));

  model->mesh_instances.emplace_back(0u, glm::mat4(1.0f));

  return model;
}

void Draw(const Shader& shader, const Mesh& mesh, const Material& material) {
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, material.ambient.texture);
  shader.setInt("material.ambient", 0);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, material.diffuse.texture);
  shader.setInt("material.diffuse", 1);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, material.specular.texture);
  shader.setInt("material.specular", 2);

  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, material.emissive.texture);
  shader.setInt("material.emissive", 3);

  shader.setFloat("material.shininess", material.shininess);

  glBindVertexArray(mesh.vao);
  glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void Draw(const Shader& shader, const Model& model,
          const glm::mat4& transform) {
  for (const auto& [meshIdx, instanceTransform] : model.mesh_instances) {
    const Mesh& mesh = model.meshes[meshIdx];
    const Material& material = model.materials[mesh.materialIndex];
    glm::mat4 final = transform * instanceTransform;
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(final)));
    shader.setMat4("model", final);
    shader.setMat3("normalMatrix", normalMatrix);
    Draw(shader, mesh, material);
  }
}

static GLuint loadCubemap(const std::string& directory) {
  const std::string suffixes[] = {"px.png", "nx.png", "py.png",
                                  "ny.png", "pz.png", "nz.png"};
  GLuint textureID;
  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

  int width, height, nrChannels;
  for (unsigned int i = 0; i < 6; i++) {
    const std::string fullPath = (exeDir() / directory / suffixes[i]).string();
    unsigned char* data =
        stbi_load(fullPath.c_str(), &width, &height, &nrChannels, 4);
    if (data) {
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, width,
                   height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
      stbi_image_free(data);
    } else {
      std::cout << "Cubemap tex failed to load at path: " << fullPath << '\n';
      unsigned char pink[] = {255, 0, 255, 255};
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, 1, 1, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, pink);
    }
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  return textureID;
}

// clang-format off
static const float skyboxVertices[] = {
    -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,

     1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,

    -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,

    -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f,
};
// clang-format on

Skybox loadSkybox(const std::string& directory) {
  // Create VAO for the skybox cube
  GLuint vao, vbo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
  glBindVertexArray(0);

  return Skybox{.vao = vao, .cubemapTexture = loadCubemap(directory)};
}
