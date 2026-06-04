#include "asset.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>
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
#include "shared/shader_constants.h"
#include "shared/util.h"

// EXT/ARB anisotropic-filtering tokens (absent from the GL 4.1 core headers).
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif

namespace {
// Anisotropy level last applied to model textures. 1 == current behavior
// (nearest-mipmap-linear min filter, no anisotropy).
int g_modelTextureAnisotropy = 1;
// Model textures that own a full mip chain (the success path below). Only
// these are touched by the runtime re-apply pass, so procedural / 1x1 fallback
// textures never become mip-incomplete.
std::vector<GLuint> g_mippedModelTextures;

float maxSupportedAnisotropy() {
  static float maxA = -1.0f;
  if (maxA < 0.0f) {
    maxA = 1.0f;
    while (glGetError() != GL_NO_ERROR) {
    }
    GLfloat v = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &v);
    if (glGetError() == GL_NO_ERROR && v > 1.0f) maxA = v;
    while (glGetError() != GL_NO_ERROR) {
    }
  }
  return maxA;
}

// level>1: trilinear + anisotropy. level<=1: the GL default
// nearest-mipmap-linear / linear (today's look). Binds tex to GL_TEXTURE_2D.
void applyMippedFilter(GLuint tex, int level) {
  glBindTexture(GL_TEXTURE_2D, tex);
  if (level > 1) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const float maxA = maxSupportedAnisotropy();
    if (maxA > 1.0f) {
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY,
                      std::min(static_cast<float>(level), maxA));
    }
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
}
}  // namespace

void setModelTextureAnisotropy(int level) {
  level = std::max(1, level);
  if (level == g_modelTextureAnisotropy) return;
  g_modelTextureAnisotropy = level;
  for (GLuint tex : g_mippedModelTextures) applyMippedFilter(tex, level);
}

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
static GLuint defaultFlatNormalTexture();

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
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void*)offsetof(Vertex, tangent));
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void*)offsetof(Vertex, bitangent));
  // Bone IDs are integers; using glVertexAttribIPointer keeps them as ivec4
  // in the shader instead of casting through floats. Locations 6/7 sit after
  // tangent/bitangent so older non-skinned shaders don't need to know about
  // them — they simply leave attribute arrays 6/7 disabled.
  glEnableVertexAttribArray(6);
  glVertexAttribIPointer(6, MAX_BONE_INFLUENCE, GL_INT, sizeof(Vertex),
                         (void*)offsetof(Vertex, boneIDs));
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, MAX_BONE_INFLUENCE, GL_FLOAT, GL_FALSE,
                        sizeof(Vertex), (void*)offsetof(Vertex, weights));
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

// Pick any unit vector perpendicular to N. Used when Assimp didn't generate
// tangents (e.g. mesh has no UVs); the value is arbitrary because the
// fallback flat normal map keeps TBN * (0,0,1) == N regardless of T/B.
static inline glm::vec3 anyPerpendicular(const glm::vec3& n) {
  glm::vec3 a = std::abs(n.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                     : glm::vec3(0.0f, 1.0f, 0.0f);
  return glm::normalize(glm::cross(n, a));
}

// AABB sphere over every mesh_instance's transformed vertex positions in the
// model's pre-orientation frame. Skinned models pad to absorb deformation;
// callers treat radius == 0 as "do not cull".
static void computeModelBounds(Model& model) {
  glm::vec3 lo(std::numeric_limits<float>::max());
  glm::vec3 hi(std::numeric_limits<float>::lowest());
  bool any = false;
  for (const auto& [meshIdx, instanceTransform] : model.mesh_instances) {
    if (meshIdx >= model.meshes.size()) continue;
    for (const auto& v : model.meshes[meshIdx].vertices) {
      glm::vec3 p = glm::vec3(instanceTransform * glm::vec4(v.position, 1.0f));
      lo = glm::min(lo, p);
      hi = glm::max(hi, p);
      any = true;
    }
  }
  if (!any) {
    model.localBoundsCenter = glm::vec3(0.0f);
    model.localBoundsRadius = 0.0f;
    return;
  }
  model.localBoundsCenter = 0.5f * (lo + hi);
  model.localBoundsRadius = glm::length(0.5f * (hi - lo));
  // Skinning can move vertices outside the bind-pose AABB; pad so we never
  // wrongly cull a deformed limb out of a shadow map.
  if (model.skinned) model.localBoundsRadius *= 1.5f;
}

static void addBoneInfluence(Vertex& v, int boneID, float weight) {
  for (int i = 0; i < MAX_BONE_INFLUENCE; ++i) {
    if (v.boneIDs[i] < 0) {
      v.boneIDs[i] = boneID;
      v.weights[i] = weight;
      return;
    }
  }
  // aiProcess_LimitBoneWeights clamps to MAX_BONE_INFLUENCE upstream; if we
  // still see overflow here, the file ships more influences than we support.
}

// Walks the mesh's bone list, assigns IDs into model->boneInfoMap, and
// writes the influences back into vertices. `model` must be non-null.
static void extractBoneWeights(const aiMesh* mesh,
                               std::vector<Vertex>& vertices, Model* model) {
  for (unsigned i = 0; i < mesh->mNumBones; ++i) {
    const aiBone* bone = mesh->mBones[i];
    std::string name = bone->mName.C_Str();
    int boneID;
    auto it = model->boneInfoMap.find(name);
    if (it == model->boneInfoMap.end()) {
      BoneInfo info;
      info.id = model->boneCount;
      info.offset = mat4_cast(bone->mOffsetMatrix);
      model->boneInfoMap[name] = info;
      boneID = model->boneCount++;
    } else {
      boneID = it->second.id;
    }
    for (unsigned w = 0; w < bone->mNumWeights; ++w) {
      const aiVertexWeight& vw = bone->mWeights[w];
      // Assimp can emit zero-weight entries; treating them as influences
      // would consume an influence slot and evict real weights.
      if (vw.mWeight == 0.0f) continue;
      if (vw.mVertexId < vertices.size()) {
        addBoneInfluence(vertices[vw.mVertexId], boneID, vw.mWeight);
      }
    }
  }
}

// `model == nullptr` for paths that can't be skinned (map sub-models share a
// VAO across nodes, so we skip bone extraction even if the source mesh has
// bones — they'd alias across sub-models otherwise).
static Mesh uploadMeshFromAi(const aiMesh* mesh, Model* model = nullptr,
                             const AssetProgressFn& progress = {}) {
  // For huge meshes (the 134k-face landscape) the CPU-side vertex and index
  // packing alone takes tens of ms; ping the progress callback every 4k
  // verts/faces so the loading cube keeps spinning at ≈60 fps inside this
  // single call.
  constexpr unsigned kProgressInterval = 4096;
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
    if (mesh->mTangents && mesh->mBitangents) {
      vertex.tangent = vec3_cast(mesh->mTangents[j]);
      vertex.bitangent = vec3_cast(mesh->mBitangents[j]);
    } else {
      vertex.tangent = anyPerpendicular(vertex.normal);
      vertex.bitangent = glm::cross(vertex.normal, vertex.tangent);
    }
    vertices.push_back(vertex);
    if (progress && (j % kProgressInterval) == 0) progress();
  }

  if (model && mesh->mNumBones > 0) {
    extractBoneWeights(mesh, vertices, model);
  }

  std::vector<GLuint> indices;
  indices.reserve(mesh->mNumFaces * 3);
  for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
    const aiFace& face = mesh->mFaces[j];
    for (unsigned int k = 0; k < face.mNumIndices; k++) {
      indices.push_back(face.mIndices[k]);
    }
    if (progress && (j % kProgressInterval) == 0) progress();
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
    // .obj exports normals under aiTextureType_HEIGHT; everything else uses
    // _NORMALS. Fall through to the shared flat default when neither exists
    // so the fragment shader can sample unconditionally.
    if (aimat->GetTextureCount(aiTextureType_NORMALS) > 0) {
      result.normal = loadMaterial(aimat, aiTextureType_NORMALS, scene);
    } else if (aimat->GetTextureCount(aiTextureType_HEIGHT) > 0) {
      result.normal = loadMaterial(aimat, aiTextureType_HEIGHT, scene);
    } else {
      result.normal = MaterialSlot{.constant = glm::vec3(0.5f, 0.5f, 1.0f),
                                   .texture = defaultFlatNormalTexture()};
    }
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

// Cheap sRGB->linear approximation. Exact piecewise formula isn't worth the
// branch — palette cluster centers don't need perceptual precision.
static inline float srgbToLinear(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Top-down RGBA8 pixel grid kept on the CPU just long enough for a model
// load to walk its triangles and collect area-weighted samples. Released
// once the corresponding loadModel / loadMapModels call returns.
struct CpuDiffuse {
  std::vector<uint8_t> rgba;  // w*h*4 bytes in source channel order
  int w = 0;
  int h = 0;
  bool isBGRA = false;
  glm::vec3 constant{1.0f};  // used when hasTexture is false
  bool hasTexture = false;
};

// Decode the material's first diffuse texture into a CpuDiffuse. Mirrors
// loadMaterial's decode paths (embedded/compressed/disk) but writes to a
// CPU buffer instead of uploading. Falls back to AI_MATKEY_COLOR_DIFFUSE
// (or white) when the material has no diffuse texture.
static CpuDiffuse decodeDiffuse(const aiMaterial* mat, const aiScene* scene) {
  CpuDiffuse out;
  if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
    aiString path;
    mat->GetTexture(aiTextureType_DIFFUSE, 0, &path);
    int channels = 0;
    uint8_t* pixels = nullptr;
    bool ownedByStb = false;
    if (auto embedded = scene->GetEmbeddedTexture(path.C_Str())) {
      if (embedded->mHeight == 0) {
        pixels = stbi_load_from_memory(
            reinterpret_cast<uint8_t*>(embedded->pcData), embedded->mWidth,
            &out.w, &out.h, &channels, 4);
        ownedByStb = pixels != nullptr;
      } else {
        out.w = embedded->mWidth;
        out.h = embedded->mHeight;
        out.isBGRA = true;
        pixels = reinterpret_cast<uint8_t*>(embedded->pcData);
      }
    } else {
      std::filesystem::path full = exeDir() / path.C_Str();
      pixels = stbi_load(full.string().c_str(), &out.w, &out.h, &channels, 4);
      ownedByStb = pixels != nullptr;
    }
    if (pixels && out.w > 0 && out.h > 0) {
      out.rgba.assign(pixels, pixels + static_cast<size_t>(out.w) * out.h * 4);
      out.hasTexture = true;
    }
    if (ownedByStb) stbi_image_free(pixels);
    return out;
  }
  aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
  mat->Get(AI_MATKEY_COLOR_DIFFUSE, color);
  out.constant = glm::vec3(srgbToLinear(color.r), srgbToLinear(color.g),
                           srgbToLinear(color.b));
  return out;
}

// Wrapped nearest-pixel sample. Matches the GPU path: aiProcess_FlipUVs is
// on, so the mesh's UVs already use the OpenGL convention where (0,0) is
// the bottom-left of the storage; index pixel rows top-down with `pyTex =
// (1 - frac(v)) * h` to mirror that.
static bool sampleDiffuseAt(const CpuDiffuse& d, glm::vec2 uv,
                            glm::vec3& outColor) {
  if (!d.hasTexture) {
    outColor = d.constant;
    return true;
  }
  if (d.w <= 0 || d.h <= 0 || d.rgba.empty()) return false;
  float fu = uv.x - std::floor(uv.x);
  float fv = uv.y - std::floor(uv.y);
  int px = std::min(d.w - 1, std::max(0, static_cast<int>(fu * d.w)));
  int py = std::min(d.h - 1, std::max(0, static_cast<int>(fv * d.h)));
  size_t idx = (static_cast<size_t>(py) * d.w + px) * 4;
  const uint8_t* p = d.rgba.data() + idx;
  if (p[3] < 8) return false;  // alpha cutout — skip transparent texels
  outColor = glm::vec3(srgbToLinear(p[d.isBGRA ? 2 : 0] / 255.0f),
                       srgbToLinear(p[1] / 255.0f),
                       srgbToLinear(p[d.isBGRA ? 0 : 2] / 255.0f));
  return true;
}

// Walk every triangle in `mesh`, sample its diffuse at four UVs (centroid +
// three vertices), and push one DiffuseSample per opaque hit weighted by
// area/4. Triangles without UVs use the material's constant color.
static void collectMeshSamples(const aiMesh* mesh, const CpuDiffuse& diffuse,
                               std::vector<DiffuseSample>& out,
                               const AssetProgressFn& progress = {}) {
  if (!mesh || mesh->mNumFaces == 0) return;
  constexpr unsigned kProgressInterval = 4096;
  const bool hasUV = mesh->mTextureCoords[0] != nullptr;
  for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
    const aiFace& face = mesh->mFaces[f];
    if (face.mNumIndices != 3) continue;
    unsigned i0 = face.mIndices[0];
    unsigned i1 = face.mIndices[1];
    unsigned i2 = face.mIndices[2];
    glm::vec3 p0 = vec3_cast(mesh->mVertices[i0]);
    glm::vec3 p1 = vec3_cast(mesh->mVertices[i1]);
    glm::vec3 p2 = vec3_cast(mesh->mVertices[i2]);
    float area = 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
    if (area <= 0.0f) continue;
    float perSampleWeight = area * 0.25f;

    glm::vec2 uv[4];
    if (hasUV) {
      uv[0] = vec2_cast(mesh->mTextureCoords[0][i0]);
      uv[1] = vec2_cast(mesh->mTextureCoords[0][i1]);
      uv[2] = vec2_cast(mesh->mTextureCoords[0][i2]);
      uv[3] = (uv[0] + uv[1] + uv[2]) / 3.0f;
    } else {
      for (auto& v : uv) v = glm::vec2(0.0f);
    }
    for (auto s : uv) {
      glm::vec3 c;
      if (sampleDiffuseAt(diffuse, s, c)) {
        out.push_back({.color = c, .weight = perSampleWeight});
      }
    }
    if (progress && (f % kProgressInterval) == 0) progress();
  }
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
      // Register for runtime anisotropy re-apply and set the current level
      // (level 1 reproduces the GL-default filtering used before this change).
      g_mippedModelTextures.push_back(id);
      applyMippedFilter(id, g_modelTextureAnisotropy);
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
  // Clamp before the u8 cast: some exporters report AI_MATKEY_COLOR_* values
  // outside [0,1] (HDR or negative), which would wrap on cast.
  auto to_u8 = [](float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
  };
  uint8_t pixel[4] = {to_u8(color.r), to_u8(color.g), to_u8(color.b),
                      to_u8(color.a)};

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
  // assimp call links on Windows. Held by shared_ptr so AnimationLibrary
  // can re-read clips from the aiScene long after this function returns.
  const std::string fullPath = (exeDir() / filename).string();
  auto parsed = std::make_shared<shared::ParsedModel>();
  if (!parsed->load(fullPath, aiProcess_Triangulate | aiProcess_FlipUVs |
                                  aiProcess_CalcTangentSpace |
                                  aiProcess_LimitBoneWeights)) {
    std::cout << "ERROR::ASSIMP::" << parsed->lastError() << '\n';
    return nullptr;
  }
  const aiScene* scene = parsed->scene();

  auto* model = new Model();
  model->materials = buildMaterials(scene);

  // CPU-side diffuse pixmaps used only here to collect area-weighted
  // samples; released as `pixmaps` goes out of scope.
  std::vector<CpuDiffuse> pixmaps;
  pixmaps.reserve(scene->mNumMaterials);
  for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
    pixmaps.push_back(decodeDiffuse(scene->mMaterials[i], scene));
  }

  for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
    const aiMesh* aiM = scene->mMeshes[i];
    model->meshes.push_back(uploadMeshFromAi(aiM, model));
    if (aiM->mMaterialIndex < pixmaps.size()) {
      collectMeshSamples(aiM, pixmaps[aiM->mMaterialIndex],
                         model->diffuseSamples);
    }
  }

  parsed->forEachMeshNode([&](const aiNode& node, const aiMatrix4x4& world) {
    glm::mat4 m = mat4_cast(world);
    for (unsigned i = 0; i < node.mNumMeshes; ++i) {
      model->mesh_instances.emplace_back(node.mMeshes[i], m);
    }
  });

  // Skinned if the mesh has bones, even without animation clips: such models
  // (e.g. the rat/goose) still render through the skinning path so a rest-pose
  // bone hierarchy + neck look-pitch override can drive head movement.
  model->skinned = model->boneCount > 0;
  model->parsed = std::move(parsed);
  if (model->boneCount > MAX_BONES) {
    std::fprintf(stderr,
                 "loadModel: '%s' has %d bones; shader caps at MAX_BONES=%d\n",
                 filename.c_str(), model->boneCount, MAX_BONES);
  }
  // Cache the neck bone name for later "look pitch" overrides. Prefer any
  // bone whose name contains "neck"; fall back to "head" so models without
  // a separate neck joint still get *some* head-tilt response.
  auto containsCI = [](const std::string& name, std::string_view needle) {
    for (size_t i = 0; i + needle.size() <= name.size(); ++i) {
      bool match = true;
      for (size_t j = 0; j < needle.size(); ++j) {
        char a = static_cast<char>(std::tolower(name[i + j]));
        char b = static_cast<char>(std::tolower(needle[j]));
        if (a != b) {
          match = false;
          break;
        }
      }
      if (match) return true;
    }
    return false;
  };
  for (const auto& [name, info] : model->boneInfoMap) {
    if (containsCI(name, "neck")) {
      model->neckBoneName = name;
      break;
    }
  }
  if (model->neckBoneName.empty()) {
    for (const auto& [name, info] : model->boneInfoMap) {
      if (containsCI(name, "head")) {
        model->neckBoneName = name;
        break;
      }
    }
  }
  computeModelBounds(*model);
  return model;
}

// 1x1 linear texture that decodes to tangent-space (0,0,1) — the identity
// normal. Materials without a normal map bind this so TBN * sample == N and
// nothing branches in the fragment shader.
static GLuint defaultFlatNormalTexture() {
  static GLuint id = 0;
  if (id != 0) return id;
  const uint8_t pixel[4] = {128, 128, 255, 255};
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixel);
  GPU_MEM_TEX2D("ModelTextures", GL_RGBA8, 1, 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return id;
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

void appendPrismFaces(std::vector<Vertex>& vertices,
                      std::vector<GLuint>& indices,
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

  GLuint diffuseTex = makeSolidTexture(def.colorR, def.colorG, def.colorB, 210);

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
  fillMat.ambient = {.constant = glm::vec3(0.55f, 0.45f, 0.65f),
                     .texture = diffuseTex};
  fillMat.diffuse = {.constant = glm::vec3(0.85f, 0.75f, 0.95f),
                     .texture = diffuseTex};
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
  // bottom, top. Corners are CCW from outside the cube so that GL_FRONT
  // matches the labeled normal (required by inverted-hull outlines).
  const Face faces[6] = {
      {.normal = {0, 0, -1},
       .corners = {{-0.5f, -0.5f, -0.5f},
                   {-0.5f, 0.5f, -0.5f},
                   {0.5f, 0.5f, -0.5f},
                   {0.5f, -0.5f, -0.5f}}},
      {.normal = {0, 0, 1},
       .corners = {{-0.5f, -0.5f, 0.5f},
                   {0.5f, -0.5f, 0.5f},
                   {0.5f, 0.5f, 0.5f},
                   {-0.5f, 0.5f, 0.5f}}},
      {.normal = {-1, 0, 0},
       .corners = {{-0.5f, -0.5f, -0.5f},
                   {-0.5f, -0.5f, 0.5f},
                   {-0.5f, 0.5f, 0.5f},
                   {-0.5f, 0.5f, -0.5f}}},
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
                   {-0.5f, 0.5f, 0.5f},
                   {0.5f, 0.5f, 0.5f},
                   {0.5f, 0.5f, -0.5f}}},
  };

  std::vector<Vertex> vertices;
  vertices.reserve(24);
  std::vector<GLuint> indices;
  indices.reserve(36);
  for (int f = 0; f < 6; f++) {
    auto base = static_cast<GLuint>(vertices.size());
    float u = (f + 0.5f) / 6.0f;
    glm::vec2 uv(u, 0.5f);
    // Arbitrary perpendicular pair — cubes bind the flat-default normal map,
    // so TBN * (0,0,1) == N regardless of which T/B we pick.
    const glm::vec3 t = anyPerpendicular(faces[f].normal);
    const glm::vec3 b = glm::cross(faces[f].normal, t);
    for (auto corner : faces[f].corners) {
      vertices.push_back({.position = corner,
                          .normal = faces[f].normal,
                          .texture_coordinates = uv,
                          .tangent = t,
                          .bitangent = b});
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
  material.normal = {.constant = glm::vec3(0.5f, 0.5f, 1.0f),
                     .texture = defaultFlatNormalTexture()};
  material.shininess = 32.0f;
  model->materials.push_back(material);

  model->meshes.push_back(buildMesh(std::move(vertices), indices, 0));
  model->mesh_instances.emplace_back(0u, glm::mat4(1.0f));
  computeModelBounds(*model);

  return model;
}

// UV sphere of radius 0.5 (matches makeCubeModel's unit-cube convention so
// RenderInfo.scale acts as a diameter). Solid-color diffuse + emissive from
// `spec`; specular/normal handled the same as makeCubeModel.
Model* makeSphereModel(const shared::CubeSpec& spec, int rings, int segments,
                       float emissiveBoost) {
  if (rings < 3) rings = 3;
  if (segments < 3) segments = 3;

  std::vector<Vertex> vertices;
  vertices.reserve(static_cast<size_t>((rings + 1) * (segments + 1)));
  for (int r = 0; r <= rings; ++r) {
    float v = static_cast<float>(r) / static_cast<float>(rings);
    float theta = v * glm::pi<float>();  // 0 (north pole) .. pi (south)
    float sinT = std::sin(theta);
    float cosT = std::cos(theta);
    for (int s = 0; s <= segments; ++s) {
      float u = static_cast<float>(s) / static_cast<float>(segments);
      float phi = u * glm::two_pi<float>();
      float sinP = std::sin(phi);
      float cosP = std::cos(phi);
      glm::vec3 n(sinT * cosP, sinT * sinP, cosT);
      glm::vec3 pos = 0.5f * n;
      // dPos/dPhi gives an east-pointing tangent.
      glm::vec3 t(-sinP, cosP, 0.0f);
      glm::vec3 b = glm::cross(n, t);
      vertices.push_back({.position = pos,
                          .normal = n,
                          .texture_coordinates = glm::vec2(u, v),
                          .tangent = t,
                          .bitangent = b});
    }
  }

  std::vector<GLuint> indices;
  indices.reserve(static_cast<size_t>(rings * segments * 6));
  const int row = segments + 1;
  for (int r = 0; r < rings; ++r) {
    for (int s = 0; s < segments; ++s) {
      auto a = static_cast<GLuint>(r * row + s);
      auto c = static_cast<GLuint>((r + 1) * row + s);
      indices.push_back(a);
      indices.push_back(c);
      indices.push_back(c + 1);
      indices.push_back(a);
      indices.push_back(c + 1);
      indices.push_back(a + 1);
    }
  }

  auto* model = new Model();

  Material material;
  GLuint diffuseTex = makeSolidTexture(spec.palette[0][0], spec.palette[0][1],
                                       spec.palette[0][2], spec.palette[0][3]);
  material.ambient = {.constant = glm::vec3(1.0f), .texture = diffuseTex};
  material.diffuse = {.constant = glm::vec3(1.0f), .texture = diffuseTex};
  material.specular = {.constant = glm::vec3(0.0f),
                       .texture = makeSolidTexture(0, 0, 0, 255)};
  material.emissive = {
      .constant = glm::vec3(emissiveBoost),
      .texture = makeSolidTexture(spec.emissive[0], spec.emissive[1],
                                  spec.emissive[2], spec.emissive[3])};
  material.normal = {.constant = glm::vec3(0.5f, 0.5f, 1.0f),
                     .texture = defaultFlatNormalTexture()};
  material.shininess = 32.0f;
  model->materials.push_back(material);

  model->meshes.push_back(buildMesh(std::move(vertices), indices, 0));
  model->mesh_instances.emplace_back(0u, glm::mat4(1.0f));
  computeModelBounds(*model);

  return model;
}

Model* makePlayerSlotCubeModel(const shared::CubeSpec& spec, uint8_t slot) {
  if (slot < 1 || slot > 4) return nullptr;

  struct Face {
    glm::vec3 normal;
    glm::vec3 corners[4];
  };
  // CCW from outside the cube so GL_FRONT matches the labeled normal —
  // required by the inverted-hull outline pass.
  const Face faces[6] = {
      {.normal = {0, 0, -1},
       .corners = {{-0.5f, -0.5f, -0.5f},
                   {-0.5f, 0.5f, -0.5f},
                   {0.5f, 0.5f, -0.5f},
                   {0.5f, -0.5f, -0.5f}}},
      {.normal = {0, 0, 1},
       .corners = {{-0.5f, -0.5f, 0.5f},
                   {0.5f, -0.5f, 0.5f},
                   {0.5f, 0.5f, 0.5f},
                   {-0.5f, 0.5f, 0.5f}}},
      {.normal = {-1, 0, 0},
       .corners = {{-0.5f, -0.5f, -0.5f},
                   {-0.5f, -0.5f, 0.5f},
                   {-0.5f, 0.5f, 0.5f},
                   {-0.5f, 0.5f, -0.5f}}},
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
                   {-0.5f, 0.5f, 0.5f},
                   {0.5f, 0.5f, 0.5f},
                   {0.5f, 0.5f, -0.5f}}},
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
      // Corner order on the top face is (-x,-z), (-x,+z), (+x,+z), (+x,-z)
      // after the winding fix; uv follows each corner's xz position.
      uv[0] = {(40.0f + 0.5f) / 48.0f, (7.0f + 0.5f) / 8.0f};
      uv[1] = {(40.0f + 0.5f) / 48.0f, (0.5f) / 8.0f};
      uv[2] = {(47.0f + 0.5f) / 48.0f, (0.5f) / 8.0f};
      uv[3] = {(47.0f + 0.5f) / 48.0f, (7.0f + 0.5f) / 8.0f};
    }
    const glm::vec3 t = anyPerpendicular(faces[f].normal);
    const glm::vec3 b = glm::cross(faces[f].normal, t);
    for (int i = 0; i < 4; i++) {
      vertices.push_back({.position = faces[f].corners[i],
                          .normal = faces[f].normal,
                          .texture_coordinates = uv[i],
                          .tangent = t,
                          .bitangent = b});
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
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void*)offsetof(Vertex, tangent));
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void*)offsetof(Vertex, bitangent));
  glEnableVertexAttribArray(6);
  glVertexAttribIPointer(6, MAX_BONE_INFLUENCE, GL_INT, sizeof(Vertex),
                         (void*)offsetof(Vertex, boneIDs));
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, MAX_BONE_INFLUENCE, GL_FLOAT, GL_FALSE,
                        sizeof(Vertex), (void*)offsetof(Vertex, weights));

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
  material.normal = {.constant = glm::vec3(0.5f, 0.5f, 1.0f),
                     .texture = defaultFlatNormalTexture()};
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
  computeModelBounds(*model);

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

  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, material.normal.texture);
  shader.setInt("material.normal", 4);

  shader.setFloat("material.shininess", material.shininess);

  glBindVertexArray(mesh.vao);
  glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void Draw(const Shader& shader, const Model& model,
          const glm::mat4& transform) {
  Draw(shader, model, transform, nullptr, 0);
}

void Draw(const Shader& shader, const Model& model, const glm::mat4& transform,
          const glm::mat4* bones, int count) {
  // Per-model palette uniform — paletteSize == 0 short-circuits the
  // quantizer in fragment_gbuffer.glsl. Setting these on shaders that
  // don't declare the uniforms (shadow/outline) is a cached -1 no-op.
  const int paletteSize = static_cast<int>(model.palette.size());
  shader.setInt("paletteSize", paletteSize);
  if (paletteSize > 0) {
    shader.setVec3Array("palette", paletteSize,
                        reinterpret_cast<const float*>(model.palette.data()));
  }

  // useSkinning gates the bone path in the vertex shader. We only upload
  // the matrices when actually used; non-skinned draws pay one setInt and
  // skip the (up to) 6.4 KB matrix upload entirely.
  const bool useSkinning = bones != nullptr && count > 0 && model.skinned;
  shader.setInt("useSkinning", useSkinning ? 1 : 0);
  if (useSkinning) {
    shader.setMat4Array("finalBonesMatrices", count,
                        reinterpret_cast<const float*>(bones));
  }

  for (const auto& [meshIdx, instanceTransform] : model.mesh_instances) {
    const Mesh& mesh = model.meshes[meshIdx];
    const Material& material = model.materials[mesh.materialIndex];
    // For skinned models the bone palette already places vertices in model
    // space; applying the per-mesh node transform on top double-applies the
    // skeleton's root scale/rotation.
    glm::mat4 final = useSkinning ? transform : transform * instanceTransform;
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(final)));
    shader.setMat4("model", final);
    shader.setMat3("normalMatrix", normalMatrix);
    Draw(shader, mesh, material);
  }
}

// Weighted k-means with deterministic strided seeding. 12 fixed iterations is
// plenty for the tiny K (<=64) we care about and avoids needing a convergence
// check. Empty samples / colors <= 0 → empty palette.
static void runWeightedKMeans(const std::vector<DiffuseSample>& samples,
                              int colors, std::vector<glm::vec3>& outPalette) {
  outPalette.clear();
  if (colors <= 0 || samples.empty()) return;

  const int maxColors = shared::kMaxPaletteColors;
  const int k = std::min({colors, maxColors, static_cast<int>(samples.size())});

  std::vector<glm::vec3> centroids(k);
  const size_t stride = std::max<size_t>(1, samples.size() / k);
  for (int i = 0; i < k; ++i) {
    centroids[i] =
        samples[(static_cast<size_t>(i) * stride) % samples.size()].color;
  }

  std::vector<glm::vec3> sums(k);
  std::vector<float> weights(k);
  constexpr int kIterations = 12;
  for (int iter = 0; iter < kIterations; ++iter) {
    std::ranges::fill(sums, glm::vec3(0.0f));
    std::ranges::fill(weights, 0.0f);
    for (const auto& s : samples) {
      int best = 0;
      float bestD = std::numeric_limits<float>::infinity();
      for (int i = 0; i < k; ++i) {
        glm::vec3 d = s.color - centroids[i];
        float dist = glm::dot(d, d);
        if (dist < bestD) {
          bestD = dist;
          best = i;
        }
      }
      sums[best] += s.color * s.weight;
      weights[best] += s.weight;
    }
    for (int i = 0; i < k; ++i) {
      if (weights[i] > 0.0f) centroids[i] = sums[i] / weights[i];
    }
  }
  outPalette = std::move(centroids);
}

void buildModelPalette(Model& model, int colors) {
  runWeightedKMeans(model.diffuseSamples, colors, model.palette);
}

void buildSkyboxPalette(Skybox& skybox, int colors) {
  runWeightedKMeans(skybox.diffuseSamples, colors, skybox.palette);
}

static GLuint loadCubemap(const std::string& directory,
                          std::vector<DiffuseSample>& outSamples,
                          const AssetProgressFn& progress) {
  const std::string suffixes[] = {"px.png", "nx.png", "py.png",
                                  "ny.png", "pz.png", "nz.png"};
  // Fixed per-face sample budget — caps total samples at 6*N*N regardless of
  // cubemap resolution so palette rebuilds stay cheap.
  constexpr int kSamplesPerFaceAxis = 32;
  outSamples.clear();
  outSamples.reserve(6 * kSamplesPerFaceAxis * kSamplesPerFaceAxis);

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
      // Stride-sample face pixels for k-means seeding. sRGB→linear so the
      // palette lives in the same color space as paletteSnap() in the shader.
      const int sw = std::max(1, width / kSamplesPerFaceAxis);
      const int sh = std::max(1, height / kSamplesPerFaceAxis);
      for (int py = 0; py < height; py += sh) {
        for (int px = 0; px < width; px += sw) {
          const uint8_t* p = data + (static_cast<size_t>(py) * width + px) * 4;
          outSamples.push_back({.color = glm::vec3(srgbToLinear(p[0] / 255.0f),
                                                   srgbToLinear(p[1] / 255.0f),
                                                   srgbToLinear(p[2] / 255.0f)),
                                .weight = 1.0f});
        }
      }
      stbi_image_free(data);
    } else {
      std::cout << "Cubemap tex failed to load at path: " << fullPath << '\n';
      unsigned char pink[] = {255, 0, 255, 255};
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_SRGB8_ALPHA8, 1, 1,
                   0, GL_RGBA, GL_UNSIGNED_BYTE, pink);
      GPU_MEM_TEX2D("SkyboxTextures", GL_SRGB8_ALPHA8, 1, 1);
    }
    if (progress) progress();
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

Skybox loadSkybox(const std::string& directory,
                  const AssetProgressFn& progress) {
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

  Skybox skybox{.vao = vao};
  skybox.cubemapTexture =
      loadCubemap(directory, skybox.diffuseSamples, progress);
  return skybox;
}

// GL-upload step for loadMapModels; expects a pre-parsed scene. Free function
// (not a member) so both `loadMapModels(filename, ...)` and the parsed-scene
// overload can share it.
static std::vector<std::pair<std::string, Model*>> uploadMapModels(
    const shared::ParsedModel& parsed, const AssetProgressFn& progress) {
  std::vector<std::pair<std::string, Model*>> out;
  const aiScene* scene = parsed.scene();
  if (!scene) return out;

  std::vector<Material> materials = buildMaterials(scene);
  if (progress) progress();

  // Diffuse pixmaps shared across all sub-models — kept alive for the
  // duration of this load only; each per-node Model below collects its own
  // weighted samples against them. Decoding each PNG can take tens of ms;
  // ping progress between materials.
  std::vector<CpuDiffuse> pixmaps;
  pixmaps.reserve(scene->mNumMaterials);
  for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
    pixmaps.push_back(decodeDiffuse(scene->mMaterials[i], scene));
    if (progress) progress();
  }

  // glTF instancing: nodes can reuse the same aiMesh — share GL handles.
  std::unordered_map<unsigned, Mesh> meshTable;
  auto getMesh = [&](unsigned sceneMeshIndex) -> const Mesh& {
    auto it = meshTable.find(sceneMeshIndex);
    if (it == meshTable.end()) {
      it = meshTable
               .emplace(sceneMeshIndex,
                        uploadMeshFromAi(scene->mMeshes[sceneMeshIndex],
                                         nullptr, progress))
               .first;
    }
    return it->second;
  };

  parsed.forEachMeshNode([&](const aiNode& node, const aiMatrix4x4&) {
    auto* model = new Model();
    model->materials = materials;
    for (unsigned i = 0; i < node.mNumMeshes; ++i) {
      unsigned sceneMeshIndex = node.mMeshes[i];
      model->meshes.push_back(getMesh(sceneMeshIndex));
      // Local transform stays identity; node world transform lives on the
      // server entity's Position + RenderInfo.scale.
      model->mesh_instances.emplace_back(
          static_cast<unsigned>(model->meshes.size() - 1), glm::mat4(1.0f));
      const aiMesh* aiM = scene->mMeshes[sceneMeshIndex];
      if (aiM->mMaterialIndex < pixmaps.size()) {
        collectMeshSamples(aiM, pixmaps[aiM->mMaterialIndex],
                           model->diffuseSamples, progress);
      }
    }
    computeModelBounds(*model);
    out.emplace_back(std::string(shared::MAP_MODEL_PREFIX) + node.mName.C_Str(),
                     model);
    if (progress) progress();
  });
  return out;
}

std::vector<std::pair<std::string, Model*>> loadMapModels(
    const std::string& filename, const AssetProgressFn& progress) {
  const std::string fullPath = (exeDir() / filename).string();
  shared::ParsedModel parsed;
  if (!parsed.load(fullPath, shared::MAP_LOAD_FLAGS)) {
    std::cout << "ERROR::ASSIMP::loadMapModels: " << parsed.lastError() << '\n';
    return {};
  }
  return uploadMapModels(parsed, progress);
}

std::vector<std::pair<std::string, Model*>> loadMapModels(
    const shared::ParsedModel& parsed, const AssetProgressFn& progress) {
  return uploadMapModels(parsed, progress);
}
