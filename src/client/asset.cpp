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
#include "client/util.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
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
        pixels = stbi_load_from_memory((uint8_t*)embedded->pcData,
                                       embedded->mWidth, &w, &h, &channels, 4);
        pixel_order = GL_RGBA;
      } else {
        w = embedded->mWidth;
        h = embedded->mHeight;
        pixel_order = GL_BGRA;
        pixels = (uint8_t*)embedded->pcData;
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
    return MaterialSlot{glm::vec3(1.0f), id};
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

  return MaterialSlot{glm::vec3(1.0f), id};
}

Model* loadModel(const std::string& filename) {
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

  for (int i = 0; i < scene->mNumMaterials; i++) {
    aiMaterial* aimat = scene->mMaterials[i];
    Material result;
    result.ambient = loadMaterial(aimat, aiTextureType_AMBIENT, scene, base);
    result.diffuse = loadMaterial(aimat, aiTextureType_DIFFUSE, scene, base);
    result.specular = loadMaterial(aimat, aiTextureType_SPECULAR, scene, base);
    result.emissive = loadMaterial(aimat, aiTextureType_EMISSIVE, scene, base);
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

Model* makeCubeModel() {
  struct Face {
    glm::vec3 normal;
    glm::vec3 corners[4];
  };
  // Face order: back, front, left, right, bottom, top (matches the palette
  // below; each face samples one texel of a 6x1 diffuse texture).
  const Face faces[6] = {
      {{0, 0, -1},
       {{-0.5f, -0.5f, -0.5f},
        {0.5f, -0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f},
        {-0.5f, 0.5f, -0.5f}}},
      {{0, 0, 1},
       {{-0.5f, -0.5f, 0.5f},
        {0.5f, -0.5f, 0.5f},
        {0.5f, 0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f}}},
      {{-1, 0, 0},
       {{-0.5f, -0.5f, -0.5f},
        {-0.5f, 0.5f, -0.5f},
        {-0.5f, 0.5f, 0.5f},
        {-0.5f, -0.5f, 0.5f}}},
      {{1, 0, 0},
       {{0.5f, -0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f},
        {0.5f, 0.5f, 0.5f},
        {0.5f, -0.5f, 0.5f}}},
      {{0, -1, 0},
       {{-0.5f, -0.5f, -0.5f},
        {0.5f, -0.5f, -0.5f},
        {0.5f, -0.5f, 0.5f},
        {-0.5f, -0.5f, 0.5f}}},
      {{0, 1, 0},
       {{-0.5f, 0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f},
        {0.5f, 0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f}}},
  };

  std::vector<Vertex> vertices;
  vertices.reserve(24);
  std::vector<GLuint> indices;
  indices.reserve(36);
  for (int f = 0; f < 6; f++) {
    GLuint base = static_cast<GLuint>(vertices.size());
    float u = (f + 0.5f) / 6.0f;
    glm::vec2 uv(u, 0.5f);
    for (int c = 0; c < 4; c++) {
      vertices.push_back({faces[f].corners[c], faces[f].normal, uv});
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
  uint8_t palette[6 * 4] = {
      204, 51,  51,  255,  // back    - red
      51,  204, 51,  255,  // front   - green
      51,  51,  204, 255,  // left    - blue
      204, 204, 51,  255,  // right   - yellow
      51,  204, 204, 255,  // bottom  - cyan
      204, 51,  204, 255,  // top     - magenta
  };
  GLuint diffuseTex;
  glGenTextures(1, &diffuseTex);
  glBindTexture(GL_TEXTURE_2D, diffuseTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 6, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               palette);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  Model* model = new Model();

  Material material;
  material.ambient = {glm::vec3(1.0f), diffuseTex};
  material.diffuse = {glm::vec3(1.0f), diffuseTex};
  material.specular = {glm::vec3(1.0f), makeSolidTexture(255, 255, 255, 255)};
  material.emissive = {glm::vec3(1.0f), makeSolidTexture(0, 0, 0, 255)};
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

void Draw(GLuint shaderProgram, const Mesh& mesh, const Material& material) {
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, material.ambient.texture);
  glUniform1i(glGetUniformLocation(shaderProgram, "material.ambient"), 0);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, material.diffuse.texture);
  glUniform1i(glGetUniformLocation(shaderProgram, "material.diffuse"), 1);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, material.specular.texture);
  glUniform1i(glGetUniformLocation(shaderProgram, "material.specular"), 2);

  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, material.emissive.texture);
  glUniform1i(glGetUniformLocation(shaderProgram, "material.emissive"), 3);

  glUniform1f(glGetUniformLocation(shaderProgram, "material.shininess"),
              material.shininess);

  glBindVertexArray(mesh.vao);
  glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
}

void Draw(GLuint shaderProgram, const Model& model,
          const glm::mat4& transform) {
  GLint transformLoc = glGetUniformLocation(shaderProgram, "model");
  for (const auto& [meshIdx, instanceTransform] : model.mesh_instances) {
    const Mesh& mesh = model.meshes[meshIdx];
    const Material& material = model.materials[mesh.materialIndex];
    glm::mat4 final = transform * instanceTransform;
    glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(final));
    Draw(shaderProgram, mesh, material);
  }
}
