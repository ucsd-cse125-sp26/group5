// Standalone inspector: loads a glTF/glb (or any assimp-readable file) and
// prints its scene summary, node tree, and material list. Used as a debugging
// aid while building out the map loader.

#include <assimp/light.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/version.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

#include "shared/mesh_loader.h"
#include "shared/util.h"

namespace {

void printIndent(int depth) {
  for (int i = 0; i < depth; ++i) std::fputs("  ", stdout);
}

std::string primitiveBits(unsigned mask) {
  std::string s;
  auto add = [&](const char* x) {
    if (!s.empty()) s += "|";
    s += x;
  };
  if (mask & aiPrimitiveType_POINT) add("POINT");
  if (mask & aiPrimitiveType_LINE) add("LINE");
  if (mask & aiPrimitiveType_TRIANGLE) add("TRIANGLE");
  if (mask & aiPrimitiveType_POLYGON) add("POLYGON");
  return s.empty() ? std::string("(none)") : s;
}

void printMetadata(const aiMetadata* md, int depth) {
  if (!md || md->mNumProperties == 0) return;
  for (unsigned i = 0; i < md->mNumProperties; ++i) {
    const aiString& key = md->mKeys[i];
    const aiMetadataEntry& e = md->mValues[i];
    printIndent(depth);
    std::printf("meta %s = ", key.C_Str());
    switch (e.mType) {
      case AI_BOOL:
        std::printf("%s (bool)\n",
                    *static_cast<bool*>(e.mData) ? "true" : "false");
        break;
      case AI_INT32:
        std::printf("%d (int32)\n", *static_cast<int32_t*>(e.mData));
        break;
      case AI_UINT64:
        std::printf("%llu (uint64)\n", static_cast<unsigned long long>(
                                           *static_cast<uint64_t*>(e.mData)));
        break;
      case AI_FLOAT:
        std::printf("%g (float)\n", *static_cast<float*>(e.mData));
        break;
      case AI_DOUBLE:
        std::printf("%g (double)\n", *static_cast<double*>(e.mData));
        break;
      case AI_AISTRING:
        std::printf("\"%s\" (string)\n",
                    static_cast<aiString*>(e.mData)->C_Str());
        break;
      case AI_AIVECTOR3D: {
        auto* v = static_cast<aiVector3D*>(e.mData);
        std::printf("(%g, %g, %g) (vec3)\n", v->x, v->y, v->z);
        break;
      }
      case AI_AIMETADATA:
        std::printf("(nested)\n");
        printMetadata(static_cast<aiMetadata*>(e.mData), depth + 1);
        break;
      default:
        std::printf("(unsupported type %d)\n", static_cast<int>(e.mType));
        break;
    }
  }
}

void printNode(const aiScene* scene, const aiNode* node, int depth) {
  printIndent(depth);
  std::printf("node \"%s\" (children=%u, meshes=%u)\n", node->mName.C_Str(),
              node->mNumChildren, node->mNumMeshes);

  if (!node->mTransformation.IsIdentity()) {
    aiVector3D t, s;
    aiQuaternion r;
    node->mTransformation.Decompose(s, r, t);
    printIndent(depth + 1);
    std::printf("xform T=(%g,%g,%g) R=(w=%g,x=%g,y=%g,z=%g) S=(%g,%g,%g)\n",
                t.x, t.y, t.z, r.w, r.x, r.y, r.z, s.x, s.y, s.z);
  }

  printMetadata(node->mMetaData, depth + 1);

  for (unsigned i = 0; i < node->mNumMeshes; ++i) {
    unsigned mi = node->mMeshes[i];
    const aiMesh* mesh = scene->mMeshes[mi];
    aiString matName;
    if (mesh->mMaterialIndex < scene->mNumMaterials) {
      scene->mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_NAME, matName);
    }
    printIndent(depth + 1);
    std::printf("mesh[%u] \"%s\" verts=%u faces=%u prim=%s mat=%u \"%s\"\n", mi,
                mesh->mName.C_Str(), mesh->mNumVertices, mesh->mNumFaces,
                primitiveBits(mesh->mPrimitiveTypes).c_str(),
                mesh->mMaterialIndex, matName.C_Str());
  }

  for (unsigned i = 0; i < node->mNumChildren; ++i) {
    printNode(scene, node->mChildren[i], depth + 1);
  }
}

const char* lightTypeName(aiLightSourceType t) {
  switch (t) {
    case aiLightSource_DIRECTIONAL:
      return "DIRECTIONAL";
    case aiLightSource_POINT:
      return "POINT";
    case aiLightSource_SPOT:
      return "SPOT";
    case aiLightSource_AMBIENT:
      return "AMBIENT";
    case aiLightSource_AREA:
      return "AREA";
    default:
      return "UNDEFINED";
  }
}

void printLight(const aiLight* light, unsigned i,
                const shared::ParsedModel& parsed) {
  std::printf("light[%u] \"%s\" type=%s\n", i, light->mName.C_Str(),
              lightTypeName(light->mType));

  // assimp pre-multiplies glTF color by intensity into all three color slots,
  // so this value can be huge (Blender exports point intensity in lumens).
  std::printf("  color*intensity=(%g,%g,%g)\n", light->mColorDiffuse.r,
              light->mColorDiffuse.g, light->mColorDiffuse.b);

  if (light->mType == aiLightSource_POINT ||
      light->mType == aiLightSource_SPOT) {
    std::printf("  attenuation: const=%g lin=%g quad=%g\n",
                light->mAttenuationConstant, light->mAttenuationLinear,
                light->mAttenuationQuadratic);
  }
  if (light->mType == aiLightSource_SPOT) {
    std::printf("  cone: inner=%g rad outer=%g rad\n", light->mAngleInnerCone,
                light->mAngleOuterCone);
  }
  if (light->mType != aiLightSource_POINT) {
    std::printf("  localDir=(%g,%g,%g) localUp=(%g,%g,%g)\n",
                light->mDirection.x, light->mDirection.y, light->mDirection.z,
                light->mUp.x, light->mUp.y, light->mUp.z);
  }

  const aiMatrix4x4* world = parsed.worldTransform(light->mName.C_Str());
  if (!world) {
    std::printf("  (no matching node)\n");
    return;
  }
  aiVector3D t, s;
  aiQuaternion r;
  world->Decompose(s, r, t);
  std::printf("  worldPos=(%g,%g,%g) attached=\"%s\"\n", t.x, t.y, t.z,
              light->mName.C_Str());

  if (light->mType != aiLightSource_POINT) {
    aiMatrix3x3 rotMat = r.GetMatrix();
    aiVector3D worldDir = rotMat * light->mDirection;
    std::printf("  worldDir=(%g,%g,%g)\n", worldDir.x, worldDir.y, worldDir.z);
  }

  // glTF's `range` is dropped from aiLight and stashed on the node instead.
  const aiNode* node = parsed.scene()->mRootNode->FindNode(light->mName);
  if (node && node->mMetaData) {
    double range;
    if (node->mMetaData->Get("PBR_LightRange", range)) {
      std::printf("  range=%g (from node metadata)\n", range);
    }
  }
}

void printMaterial(const aiMaterial* mat, unsigned i) {
  aiString name;
  mat->Get(AI_MATKEY_NAME, name);
  std::printf("material[%u] \"%s\"", i, name.C_Str());

  aiColor4D diffuse;
  if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
    std::printf(" diffuse=(%g,%g,%g,%g)", diffuse.r, diffuse.g, diffuse.b,
                diffuse.a);
  }
  aiColor4D base;
  if (mat->Get(AI_MATKEY_BASE_COLOR, base) == AI_SUCCESS) {
    std::printf(" base=(%g,%g,%g,%g)", base.r, base.g, base.b, base.a);
  }
  std::printf("\n");

  static const std::pair<aiTextureType, const char*> kinds[] = {
      {aiTextureType_DIFFUSE, "diffuse"},
      {aiTextureType_SPECULAR, "specular"},
      {aiTextureType_NORMALS, "normals"},
      {aiTextureType_EMISSIVE, "emissive"},
      {aiTextureType_HEIGHT, "height"},
      {aiTextureType_LIGHTMAP, "lightmap"},
      {aiTextureType_BASE_COLOR, "baseColor"},
      {aiTextureType_METALNESS, "metalness"},
      {aiTextureType_DIFFUSE_ROUGHNESS, "roughness"},
      {aiTextureType_AMBIENT_OCCLUSION, "ao"},
      {aiTextureType_UNKNOWN, "unknown"},
  };
  for (auto [t, label] : kinds) {
    unsigned n = mat->GetTextureCount(t);
    if (n == 0) continue;
    aiString path;
    mat->GetTexture(t, 0, &path);
    std::printf("  tex %s x%u: \"%s\"\n", label, n, path.C_Str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path path;
  if (argc >= 2) {
    path = argv[1];
  } else {
    path = exeDir() / "assets" / "rebecca" / "CSE125Test.glb";
  }

  std::printf("gltf_inspect (assimp %u.%u.%u)\n", aiGetVersionMajor(),
              aiGetVersionMinor(), aiGetVersionPatch());
  std::printf("file: %s\n", path.string().c_str());

  shared::ParsedModel parsed;
  if (!parsed.load(path.string(), aiProcess_Triangulate | aiProcess_FlipUVs)) {
    std::fprintf(stderr, "ERROR::ASSIMP:: %s\n", parsed.lastError().c_str());
    return 1;
  }
  const aiScene* scene = parsed.scene();

  std::printf(
      "scene: meshes=%u materials=%u textures=%u animations=%u cameras=%u "
      "lights=%u\n",
      scene->mNumMeshes, scene->mNumMaterials, scene->mNumTextures,
      scene->mNumAnimations, scene->mNumCameras, scene->mNumLights);
  if (scene->mMetaData && scene->mMetaData->mNumProperties > 0) {
    std::printf("scene metadata:\n");
    printMetadata(scene->mMetaData, 1);
  }

  std::printf("\nnode tree:\n");
  printNode(scene, scene->mRootNode, 0);

  std::printf("\nmaterials:\n");
  for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
    printMaterial(scene->mMaterials[i], i);
  }

  if (scene->mNumLights > 0) {
    std::printf("\nlights:\n");
    for (unsigned i = 0; i < scene->mNumLights; ++i) {
      printLight(scene->mLights[i], i, parsed);
    }
  }

  return 0;
}
