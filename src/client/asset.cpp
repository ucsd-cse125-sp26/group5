#include "asset.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "assimp/material.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "client/shaders.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "shared/map_format.h"
#include "shared/mesh_loader.h"
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
                          const aiScene* scene);

// Uploads vertex/index buffers and binds the standard 3-attribute VAO layout
// (position/normal/texCoord). Caller fills the vectors; this owns the GL
// resource creation.
static Mesh buildMesh(std::vector<Vertex> vertices,
                      const std::vector<GLuint>& indices,
                      unsigned materialIndex) {
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
  m.materialIndex = materialIndex;
  m.vao = vao;
  m.vbo = vbo;
  m.ebo = ebo;
  m.index_count = static_cast<GLuint>(indices.size());
  return m;
}

static Mesh uploadMeshFromAi(const aiMesh* mesh) {
  std::vector<Vertex> vertices;
  vertices.reserve(mesh->mNumVertices);
  for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
    Vertex vertex;
    vertex.position = vec3_cast(mesh->mVertices[j]);
    vertex.normal = mesh->mNormals ? vec3_cast(mesh->mNormals[j])
                                   : glm::vec3(0.0f, 0.0f, 1.0f);
    vertex.texture_coordinates = mesh->mTextureCoords[0]
                                     ? vec2_cast(mesh->mTextureCoords[0][j])
                                     : glm::vec2(0.0f);
    vertices.push_back(vertex);
  }

  std::vector<GLuint> indices;
  indices.reserve(mesh->mNumFaces * 3);
  for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
    const aiFace& face = mesh->mFaces[j];
    for (unsigned int k = 0; k < face.mNumIndices; k++) {
      indices.push_back(face.mIndices[k]);
    }
  }

  return buildMesh(std::move(vertices), indices, mesh->mMaterialIndex);
}

static std::vector<Material> buildMaterials(const aiScene* scene) {
  std::vector<Material> out;
  out.reserve(scene->mNumMaterials);
  for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
    aiMaterial* aimat = scene->mMaterials[i];
    Material result;
    result.ambient = loadMaterial(aimat, aiTextureType_AMBIENT, scene);
    result.diffuse = loadMaterial(aimat, aiTextureType_DIFFUSE, scene);
    result.specular = loadMaterial(aimat, aiTextureType_SPECULAR, scene);
    result.emissive = loadMaterial(aimat, aiTextureType_EMISSIVE, scene);
    aimat->Get(AI_MATKEY_SHININESS, result.shininess);
    out.push_back(result);
  }
  return out;
}

MaterialSlot loadMaterial(const aiMaterial* mat, aiTextureType type,
                          const aiScene* scene) {
  if (mat->GetTextureCount(type) > 0) {
    aiString path;
    mat->GetTexture(type, 0, &path);

    int w = 0, h = 0, channels = 0;
    GLenum pixelOrder = GL_RGBA;
    uint8_t* pixels = nullptr;
    bool ownedByStb = false;
    if (auto embedded = scene->GetEmbeddedTexture(path.C_Str())) {
      if (embedded->mHeight == 0) {
        // Compressed embedded blob; mWidth is the byte length.
        pixels =
            stbi_load_from_memory(reinterpret_cast<uint8_t*>(embedded->pcData),
                                  embedded->mWidth, &w, &h, &channels, 4);
        ownedByStb = pixels != nullptr;
      } else {
        // Uncompressed BGRA8 directly in scene memory.
        w = embedded->mWidth;
        h = embedded->mHeight;
        pixelOrder = GL_BGRA;
        pixels = reinterpret_cast<uint8_t*>(embedded->pcData);
      }
    } else {
      std::filesystem::path full = exeDir() / path.C_Str();
      pixels = stbi_load(full.string().c_str(), &w, &h, &channels, 4);
      ownedByStb = pixels != nullptr;
    }

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    if (pixels) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, pixelOrder,
                   GL_UNSIGNED_BYTE, pixels);
      glGenerateMipmap(GL_TEXTURE_2D);
    } else {
      std::fprintf(stderr,
                   "loadMaterial: failed to decode texture \"%s\" "
                   "(type=%d); using magenta fallback\n",
                   path.C_Str(), static_cast<int>(type));
      uint8_t magenta[4] = {255, 0, 255, 255};
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, magenta);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    if (ownedByStb) stbi_image_free(pixels);
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
  const std::string fullPath = (exeDir() / filename).string();
  shared::ParsedModel parsed;
  if (!parsed.load(fullPath, aiProcess_Triangulate | aiProcess_FlipUVs)) {
    std::cout << "ERROR::ASSIMP::" << parsed.lastError() << '\n';
    return nullptr;
  }
  const aiScene* scene = parsed.scene();

  auto* model = new Model();
  model->materials = buildMaterials(scene);

  // Upload every aiMesh in scene-mesh order; mesh_instances below indexes
  // into model->meshes via the same scene-mesh index.
  for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
    model->meshes.push_back(uploadMeshFromAi(scene->mMeshes[i]));
  }

  parsed.forEachMeshNode([&](const aiNode& node, const aiMatrix4x4& world) {
    glm::mat4 m = mat4_cast(world);
    for (unsigned i = 0; i < node.mNumMeshes; ++i) {
      model->mesh_instances.emplace_back(node.mMeshes[i], m);
    }
  });

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

  model->meshes.push_back(buildMesh(std::move(vertices), indices, 0));
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

std::vector<std::pair<std::string, Model*>> loadMapModels(
    const std::string& filename) {
  std::vector<std::pair<std::string, Model*>> out;
  const std::string fullPath = (exeDir() / filename).string();
  shared::ParsedModel parsed;
  if (!parsed.load(fullPath, shared::MAP_LOAD_FLAGS)) {
    std::cout << "ERROR::ASSIMP::loadMapModels: " << parsed.lastError() << '\n';
    return out;
  }
  const aiScene* scene = parsed.scene();

  std::vector<Material> materials = buildMaterials(scene);

  // Memoize so multiple nodes referencing the same aiMesh (glTF instancing)
  // share VAO/VBO/EBO handles instead of each building their own.
  std::unordered_map<unsigned, Mesh> meshTable;
  auto getMesh = [&](unsigned sceneMeshIndex) -> const Mesh& {
    auto it = meshTable.find(sceneMeshIndex);
    if (it == meshTable.end()) {
      it = meshTable
               .emplace(sceneMeshIndex,
                        uploadMeshFromAi(scene->mMeshes[sceneMeshIndex]))
               .first;
    }
    return it->second;
  };

  parsed.forEachMeshNode([&](const aiNode& node, const aiMatrix4x4&) {
    auto* model = new Model();
    model->materials = materials;
    for (unsigned i = 0; i < node.mNumMeshes; ++i) {
      model->meshes.push_back(getMesh(node.mMeshes[i]));
      // Identity local transform — node's world transform lives on the
      // server-spawned entity's Position + RenderInfo.scale.
      model->mesh_instances.emplace_back(
          static_cast<unsigned>(model->meshes.size() - 1), glm::mat4(1.0f));
    }
    out.emplace_back(std::string(shared::MAP_MODEL_PREFIX) + node.mName.C_Str(),
                     model);
  });
  return out;
}
