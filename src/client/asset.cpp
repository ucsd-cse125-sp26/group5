#include "asset.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cctype>
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
#include "shared/gpu_mem_profiler.h"
#include "shared/map_format.h"
#include "shared/mesh_loader.h"
#include "shared/puzzles/tangram/puzzle_data.h"
#include "shared/util.h"

static inline glm::vec3 vec3_cast(const aiVector3D& v) {
  return {v.x, v.y, v.z};
}
// aiVector3D because assimp stores UVs in 3-component vectors.
static inline glm::vec2 vec2_cast(const aiVector3D& v) { return {v.x, v.y}; }
static inline glm::mat4 mat4_cast(const aiMatrix4x4& m) {
  return glm::transpose(glm::make_mat4(&m.a1));
}

MaterialSlot loadMaterial(const aiMaterial* mat, aiTextureType type,
                          const aiScene* scene);

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
  GPU_MEM_ADD("ModelGeometry", vertices.size() * sizeof(Vertex));
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint),
               indices.data(), GL_STATIC_DRAW);
  GPU_MEM_ADD("ModelGeometry", indices.size() * sizeof(GLuint));

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
    // glTF reports shininess=0 (it uses roughness, not Phong). Treat 0 as
    // "absent" so pow(_, 0)=1 doesn't pin specular at max and blow out the
    // surface to white.
    float shininess = 0.0f;
    if (aimat->Get(AI_MATKEY_SHININESS, shininess) == aiReturn_SUCCESS &&
        shininess > 0.0f) {
      result.shininess = shininess;
    }
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
        // Compressed blob; mWidth is the byte length.
        pixels =
            stbi_load_from_memory(reinterpret_cast<uint8_t*>(embedded->pcData),
                                  embedded->mWidth, &w, &h, &channels, 4);
        ownedByStb = pixels != nullptr;
      } else {
        // Uncompressed BGRA8 in scene memory.
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

    // sRGB upload for perceptual channels so the GPU samples them linearly;
    // specular/ambient are masks and stay in linear RGBA8.
    const bool perceptual =
        type == aiTextureType_DIFFUSE || type == aiTextureType_EMISSIVE;
    const GLint internal = perceptual ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    if (pixels) {
      glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, pixelOrder,
                   GL_UNSIGNED_BYTE, pixels);
      glGenerateMipmap(GL_TEXTURE_2D);
      GPU_MEM_TEX2D_MIPPED("ModelTextures", internal, w, h);
    } else {
      std::fprintf(stderr,
                   "loadMaterial: failed to decode texture \"%s\" "
                   "(type=%d); using magenta fallback\n",
                   path.C_Str(), static_cast<int>(type));
      uint8_t magenta[4] = {255, 0, 255, 255};
      glTexImage2D(GL_TEXTURE_2D, 0, internal, 1, 1, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, magenta);
      GPU_MEM_TEX2D("ModelTextures", internal, 1, 1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    if (ownedByStb) stbi_image_free(pixels);
    return MaterialSlot{.constant = glm::vec3(1.0f), .texture = id};
  }

  // Per-type default: white for DIFFUSE so untextured meshes are visible;
  // black for SPECULAR/EMISSIVE/AMBIENT so missing properties don't blow
  // out the surface (white emissive would add 1.0 to every fragment).
  aiColor4D color = (type == aiTextureType_DIFFUSE)
                        ? aiColor4D(1.0f, 1.0f, 1.0f, 1.0f)
                        : aiColor4D(0.0f, 0.0f, 0.0f, 1.0f);
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

  // Same sRGB convention as the texture path so constant and texture
  // fallbacks stay gamma-consistent.
  const bool perceptual =
      type == aiTextureType_DIFFUSE || type == aiTextureType_EMISSIVE;
  const GLint internal = perceptual ? GL_SRGB8_ALPHA8 : GL_RGBA8;
  GLuint id;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D, 0, internal, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixel);
  GPU_MEM_TEX2D("ModelTextures", internal, 1, 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  return MaterialSlot{.constant = glm::vec3(1.0f), .texture = id};
}

Model* loadModel(const std::string& filename) {
  // MinGW's path::string_type is wstring; convert explicitly so the
  // assimp call links on Windows.
  const std::string fullPath = (exeDir() / filename).string();
  shared::ParsedModel parsed;
  if (!parsed.load(fullPath, aiProcess_Triangulate | aiProcess_FlipUVs)) {
    std::cout << "ERROR::ASSIMP::" << parsed.lastError() << '\n';
    return nullptr;
  }
  const aiScene* scene = parsed.scene();

  auto* model = new Model();
  model->materials = buildMaterials(scene);

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
  GPU_MEM_TEX2D("ModelTextures", GL_RGBA8, 1, 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return id;
}


namespace {

void tangramFootprint(const shared::tangram_puzzle::PieceDef& def,
                      std::vector<glm::vec2>& out) {
  out.clear();
  switch (def.shape) {
    case shared::tangram_puzzle::PieceShape::LargeTriangle:
      out = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}};
      break;
    case shared::tangram_puzzle::PieceShape::MediumTriangle:
      out = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}};
      break;
    case shared::tangram_puzzle::PieceShape::SmallTriangle:
      out = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}};
      break;
    case shared::tangram_puzzle::PieceShape::Square:
      out = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
      break;
    case shared::tangram_puzzle::PieceShape::Parallelogram: {
      const float w = 0.42f;
      const float h = 0.28f;
      const float sl = 0.22f;
      out = {{-w, -h}, {w, -h}, {w + sl, h}, {-w + sl, h}};
      break;
    }
  }
}

void appendPrismFaces(std::vector<Vertex>& vertices, std::vector<GLuint>& indices,
                      const std::vector<glm::vec2>& footprint, float z0,
                      float z1, const glm::vec2& uv) {
  if (footprint.size() < 3) return;
  const int n = static_cast<int>(footprint.size());

  auto pushTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 normal) {
    const auto base = static_cast<GLuint>(vertices.size());
    vertices.push_back({a, normal, uv});
    vertices.push_back({b, normal, uv});
    vertices.push_back({c, normal, uv});
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
  };

  for (int i = 1; i < n - 1; ++i) {
    pushTri({footprint[0].x, footprint[0].y, z1},
            {footprint[static_cast<size_t>(i)].x,
             footprint[static_cast<size_t>(i)].y, z1},
            {footprint[static_cast<size_t>(i + 1)].x,
             footprint[static_cast<size_t>(i + 1)].y, z1},
            {0, 0, 1});
    pushTri({footprint[0].x, footprint[0].y, z0},
            {footprint[static_cast<size_t>(i + 1)].x,
             footprint[static_cast<size_t>(i + 1)].y, z0},
            {footprint[static_cast<size_t>(i)].x,
             footprint[static_cast<size_t>(i)].y, z0},
            {0, 0, -1});
  }

  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    const glm::vec2 a = footprint[static_cast<size_t>(i)];
    const glm::vec2 b = footprint[static_cast<size_t>(j)];
    glm::vec3 edge(b.x - a.x, b.y - a.y, 0.0f);
    glm::vec3 normal = glm::normalize(glm::cross(edge, glm::vec3(0, 0, 1)));
    const auto base = static_cast<GLuint>(vertices.size());
    vertices.push_back({{a.x, a.y, z0}, normal, uv});
    vertices.push_back({{b.x, b.y, z0}, normal, uv});
    vertices.push_back({{b.x, b.y, z1}, normal, uv});
    vertices.push_back({{a.x, a.y, z1}, normal, uv});
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
  }
}

}  // namespace

Model* makeTangramPieceModel(const shared::tangram_puzzle::PieceDef& def) {
  std::vector<glm::vec2> footprint;
  tangramFootprint(def, footprint);
  if (footprint.empty()) return nullptr;

  std::vector<Vertex> vertices;
  std::vector<GLuint> indices;
  vertices.reserve(64);
  indices.reserve(96);
  appendPrismFaces(vertices, indices, footprint, -0.5f, 0.5f, {0.5f, 0.5f});

  GLuint diffuseTex = makeSolidTexture(def.colorR, def.colorG, def.colorB, 255);

  auto* model = new Model();
  Material material;
  material.ambient = {.constant = glm::vec3(1.0f), .texture = diffuseTex};
  material.diffuse = {.constant = glm::vec3(1.0f), .texture = diffuseTex};
  material.specular = {.constant = glm::vec3(0.0f),
                       .texture = makeSolidTexture(0, 0, 0, 255)};
  material.emissive = {.constant = glm::vec3(0.0f),
                       .texture = makeSolidTexture(0, 0, 0, 255)};
  material.shininess = 12.0f;
  model->materials.push_back(material);
  model->meshes.push_back(buildMesh(std::move(vertices), indices, 0));
  model->mesh_instances.emplace_back(0u, glm::mat4(1.0f));
  return model;
}

Model* makeTangramPieceMuteModel(const shared::tangram_puzzle::PieceDef& def) {
  std::vector<glm::vec2> footprint;
  tangramFootprint(def, footprint);
  if (footprint.empty()) return nullptr;

  std::vector<Vertex> vertices;
  std::vector<GLuint> indices;
  appendPrismFaces(vertices, indices, footprint, -0.5f, 0.5f, {0.5f, 0.5f});

  GLuint diffuseTex = makeSolidTexture(118, 118, 128, 255);

  auto* model = new Model();
  Material material;
  material.ambient = {.constant = glm::vec3(0.7f), .texture = diffuseTex};
  material.diffuse = {.constant = glm::vec3(0.85f), .texture = diffuseTex};
  material.specular = {.constant = glm::vec3(0.0f),
                       .texture = makeSolidTexture(0, 0, 0, 255)};
  material.emissive = {.constant = glm::vec3(0.0f),
                       .texture = makeSolidTexture(0, 0, 0, 255)};
  material.shininess = 8.0f;
  model->materials.push_back(material);
  model->meshes.push_back(buildMesh(std::move(vertices), indices, 0));
  model->mesh_instances.emplace_back(0u, glm::mat4(1.0f));
  return model;
}

Model* makeTangramColoredGhostSlotModel(
    const shared::tangram_puzzle::PieceDef& def) {
  std::vector<glm::vec2> footprint;
  tangramFootprint(def, footprint);
  if (footprint.empty()) return nullptr;

  std::vector<Vertex> vertices;
  std::vector<GLuint> indices;
  appendPrismFaces(vertices, indices, footprint, -0.5f, 0.5f, {0.5f, 0.5f});

  GLuint diffuseTex =
      makeSolidTexture(def.colorR, def.colorG, def.colorB, 210);

  auto* model = new Model();
  Material fillMat;
  const glm::vec3 tint(def.colorR / 255.0f, def.colorG / 255.0f,
                       def.colorB / 255.0f);
  fillMat.ambient = {.constant = tint * 0.65f, .texture = diffuseTex};
  fillMat.diffuse = {.constant = tint * 0.95f, .texture = diffuseTex};
  fillMat.specular = {.constant = glm::vec3(0.0f),
                      .texture = makeSolidTexture(0, 0, 0, 255)};
  fillMat.emissive = {.constant = tint * 0.28f, .texture = diffuseTex};
  fillMat.shininess = 4.0f;
  model->materials.push_back(fillMat);
  model->meshes.push_back(buildMesh(std::move(vertices), indices, 0));
  model->mesh_instances.emplace_back(0u, glm::mat4(1.0f));
  return model;
}

Model* makeTangramGhostSlotModel(const shared::tangram_puzzle::PieceDef& def) {
  std::vector<glm::vec2> footprint;
  tangramFootprint(def, footprint);
  if (footprint.empty()) return nullptr;

  std::vector<Vertex> vertices;
  std::vector<GLuint> indices;
  appendPrismFaces(vertices, indices, footprint, -0.5f, 0.5f, {0.5f, 0.5f});

  GLuint diffuseTex = makeSolidTexture(48, 42, 58, 235);

  auto* model = new Model();
  Material fillMat;
  fillMat.ambient = {.constant = glm::vec3(0.55f, 0.45f, 0.65f), .texture = diffuseTex};
  fillMat.diffuse = {.constant = glm::vec3(0.85f, 0.75f, 0.95f), .texture = diffuseTex};
  fillMat.specular = {.constant = glm::vec3(0.0f),
                      .texture = makeSolidTexture(0, 0, 0, 255)};
  fillMat.emissive = {.constant = glm::vec3(0.45f, 0.38f, 0.55f),
                      .texture = makeSolidTexture(0, 0, 0, 255)};
  fillMat.shininess = 2.0f;
  model->materials.push_back(fillMat);
  model->meshes.push_back(buildMesh(std::move(vertices), indices, 0));
  model->mesh_instances.emplace_back(0u, glm::mat4(1.0f));
  return model;
}

Model* makeCubeModel(const shared::CubeSpec& spec) {
  struct Face {
    glm::vec3 normal;
    glm::vec3 corners[4];
  };
  // Face order matches the 6x1 palette below: back, front, left, right,
  // bottom, top.
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

  // 6x1 sRGB palette, one texel per face (faces[] order).
  GLuint diffuseTex;
  glGenTextures(1, &diffuseTex);
  glBindTexture(GL_TEXTURE_2D, diffuseTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, 6, 1, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, spec.palette);
  GPU_MEM_TEX2D("ModelTextures", GL_SRGB8_ALPHA8, 6, 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  auto* model = new Model();

  Material material;
  material.ambient = {.constant = glm::vec3(1.0f), .texture = diffuseTex};
  material.diffuse = {.constant = glm::vec3(1.0f), .texture = diffuseTex};
  // Match the Assimp path's "missing = black" convention; white here pins
  // specular to max on every surface and blooms blow out the frame.
  material.specular = {.constant = glm::vec3(0.0f),
                       .texture = makeSolidTexture(0, 0, 0, 255)};
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

Model* makePlayerSlotCubeModel(const shared::CubeSpec& spec, uint8_t slot) {
  if (slot < 1 || slot > 4) return nullptr;

  struct Face {
    glm::vec3 normal;
    glm::vec3 corners[4];
  };
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

  static const uint8_t kPatterns[4][5][3] = {
      {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}},
      {{1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {1, 0, 0}, {1, 1, 1}},
      {{1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {0, 0, 1}, {1, 1, 1}},
      {{1, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 0, 1}, {0, 0, 1}},
  };

  std::vector<uint8_t> pixels(48 * 8 * 4, 0);
  auto setPx = [&](int x, int y, const uint8_t rgba[4]) {
    int o = (y * 48 + x) * 4;
    pixels[static_cast<size_t>(o)] = rgba[0];
    pixels[static_cast<size_t>(o + 1)] = rgba[1];
    pixels[static_cast<size_t>(o + 2)] = rgba[2];
    pixels[static_cast<size_t>(o + 3)] = rgba[3];
  };

  for (int f = 0; f < 5; f++) {
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        setPx(f * 8 + x, y, spec.palette[f]);
      }
    }
  }
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      setPx(40 + x, y, spec.palette[5]);
    }
  }

  const uint8_t* top = spec.palette[5];
  const bool topBright = top[0] > 160 && top[1] > 160 && top[2] > 160;
  const auto dr = static_cast<uint8_t>(topBright ? 30 : 250);
  const auto dg = static_cast<uint8_t>(topBright ? 30 : 250);
  const auto db = static_cast<uint8_t>(topBright ? 30 : 250);
  const uint8_t digitRgba[4] = {dr, dg, db, 255};
  const auto di = static_cast<uint8_t>(slot - 1);
  for (int dy = 0; dy < 5; dy++) {
    for (int dx = 0; dx < 3; dx++) {
      if (kPatterns[di][static_cast<size_t>(dy)][static_cast<size_t>(dx)] !=
          0) {
        const int ty = 6 - dy;
        setPx(40 + 2 + dx, ty, digitRgba);
      }
    }
  }

  std::vector<Vertex> vertices;
  vertices.reserve(24);
  std::vector<GLuint> indices;
  indices.reserve(36);
  for (int f = 0; f < 6; f++) {
    auto base = static_cast<GLuint>(vertices.size());
    glm::vec2 uv[4];
    if (f < 5) {
      const float u = (static_cast<float>(f * 8 + 4) + 0.5f) / 48.0f;
      const float v = (4.0f + 0.5f) / 8.0f;
      for (auto& i : uv) i = {u, v};
    } else {
      uv[0] = {(40.0f + 0.5f) / 48.0f, (7.0f + 0.5f) / 8.0f};
      uv[1] = {(47.0f + 0.5f) / 48.0f, (7.0f + 0.5f) / 8.0f};
      uv[2] = {(47.0f + 0.5f) / 48.0f, (0.5f) / 8.0f};
      uv[3] = {(40.0f + 0.5f) / 48.0f, (0.5f) / 8.0f};
    }
    for (int i = 0; i < 4; i++) {
      vertices.push_back({.position = faces[f].corners[i],
                          .normal = faces[f].normal,
                          .texture_coordinates = uv[i]});
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

  GLuint diffuseTex;
  glGenTextures(1, &diffuseTex);
  glBindTexture(GL_TEXTURE_2D, diffuseTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 48, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels.data());
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
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_SRGB8_ALPHA8,
                   width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
      GPU_MEM_TEX2D("SkyboxTextures", GL_SRGB8_ALPHA8, width, height);
      stbi_image_free(data);
    } else {
      std::cout << "Cubemap tex failed to load at path: " << fullPath << '\n';
      unsigned char pink[] = {255, 0, 255, 255};
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_SRGB8_ALPHA8, 1, 1,
                   0, GL_RGBA, GL_UNSIGNED_BYTE, pink);
      GPU_MEM_TEX2D("SkyboxTextures", GL_SRGB8_ALPHA8, 1, 1);
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
  GLuint vao, vbo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices,
               GL_STATIC_DRAW);
  GPU_MEM_ADD("SkyboxGeometry", sizeof(skyboxVertices));
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

  // glTF instancing: nodes can reuse the same aiMesh — share GL handles.
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
      // Local transform stays identity; node world transform lives on the
      // server entity's Position + RenderInfo.scale.
      model->mesh_instances.emplace_back(
          static_cast<unsigned>(model->meshes.size() - 1), glm::mat4(1.0f));
    }
    out.emplace_back(std::string(shared::MAP_MODEL_PREFIX) + node.mName.C_Str(),
                     model);
  });
  return out;
}
