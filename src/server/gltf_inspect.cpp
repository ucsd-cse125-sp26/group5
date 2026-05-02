// Standalone inspector — prints scene/node/material/light/camera summary for
// any assimp-readable file, including a full dump of scene + node metadata
// and every aiMaterialProperty. Diagnostic aid for the map loader.

#include <assimp/camera.h>
#include <assimp/light.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/version.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
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

  // glTF stashes per-light extras (e.g. `range`) on the matching node's
  // metadata. Dump the full block — printNode already shows it once during the
  // tree walk, but reprinting here keeps light diagnostics self-contained.
  const aiNode* node = parsed.scene()->mRootNode->FindNode(light->mName);
  if (node && node->mMetaData && node->mMetaData->mNumProperties > 0) {
    std::printf("  node metadata:\n");
    printMetadata(node->mMetaData, 2);
  }
}

void printCamera(const aiCamera* cam, unsigned i,
                 const shared::ParsedModel& parsed) {
  std::printf(
      "camera[%u] \"%s\" hfov=%g rad aspect=%g near=%g far=%g\n", i,
      cam->mName.C_Str(), cam->mHorizontalFOV, cam->mAspect, cam->mClipPlaneNear,
      cam->mClipPlaneFar);
  std::printf("  localPos=(%g,%g,%g) localLook=(%g,%g,%g) localUp=(%g,%g,%g)\n",
              cam->mPosition.x, cam->mPosition.y, cam->mPosition.z,
              cam->mLookAt.x, cam->mLookAt.y, cam->mLookAt.z, cam->mUp.x,
              cam->mUp.y, cam->mUp.z);

  const aiMatrix4x4* world = parsed.worldTransform(cam->mName.C_Str());
  if (world) {
    aiVector3D t, s;
    aiQuaternion r;
    world->Decompose(s, r, t);
    std::printf("  worldPos=(%g,%g,%g)\n", t.x, t.y, t.z);
  }

  const aiNode* node = parsed.scene()->mRootNode->FindNode(cam->mName);
  if (node && node->mMetaData && node->mMetaData->mNumProperties > 0) {
    std::printf("  node metadata:\n");
    printMetadata(node->mMetaData, 2);
  }
}

const char* propertyTypeName(aiPropertyTypeInfo t) {
  switch (t) {
    case aiPTI_Float:
      return "float";
    case aiPTI_Double:
      return "double";
    case aiPTI_String:
      return "string";
    case aiPTI_Integer:
      return "int";
    case aiPTI_Buffer:
      return "buffer";
    default:
      return "?";
  }
}

const char* textureTypeName(unsigned semantic) {
  switch (semantic) {
    case aiTextureType_NONE:
      return "";
    case aiTextureType_DIFFUSE:
      return "DIFFUSE";
    case aiTextureType_SPECULAR:
      return "SPECULAR";
    case aiTextureType_AMBIENT:
      return "AMBIENT";
    case aiTextureType_EMISSIVE:
      return "EMISSIVE";
    case aiTextureType_HEIGHT:
      return "HEIGHT";
    case aiTextureType_NORMALS:
      return "NORMALS";
    case aiTextureType_SHININESS:
      return "SHININESS";
    case aiTextureType_OPACITY:
      return "OPACITY";
    case aiTextureType_DISPLACEMENT:
      return "DISPLACEMENT";
    case aiTextureType_LIGHTMAP:
      return "LIGHTMAP";
    case aiTextureType_REFLECTION:
      return "REFLECTION";
    case aiTextureType_BASE_COLOR:
      return "BASE_COLOR";
    case aiTextureType_NORMAL_CAMERA:
      return "NORMAL_CAMERA";
    case aiTextureType_EMISSION_COLOR:
      return "EMISSION_COLOR";
    case aiTextureType_METALNESS:
      return "METALNESS";
    case aiTextureType_DIFFUSE_ROUGHNESS:
      return "DIFFUSE_ROUGHNESS";
    case aiTextureType_AMBIENT_OCCLUSION:
      return "AMBIENT_OCCLUSION";
    case aiTextureType_SHEEN:
      return "SHEEN";
    case aiTextureType_CLEARCOAT:
      return "CLEARCOAT";
    case aiTextureType_TRANSMISSION:
      return "TRANSMISSION";
    case aiTextureType_UNKNOWN:
      return "UNKNOWN";
    default:
      return "?";
  }
}

void printMaterialProperties(const aiMaterial* mat) {
  for (unsigned p = 0; p < mat->mNumProperties; ++p) {
    const aiMaterialProperty* prop = mat->mProperties[p];
    const char* tex = textureTypeName(prop->mSemantic);
    if (*tex) {
      std::printf("  prop \"%s\" [%s/%u] (%s,len=%u): ", prop->mKey.C_Str(), tex,
                  prop->mIndex, propertyTypeName(prop->mType),
                  prop->mDataLength);
    } else {
      std::printf("  prop \"%s\" (%s,len=%u): ", prop->mKey.C_Str(),
                  propertyTypeName(prop->mType), prop->mDataLength);
    }
    switch (prop->mType) {
      case aiPTI_Float: {
        unsigned n = prop->mDataLength / sizeof(float);
        const float* f = reinterpret_cast<const float*>(prop->mData);
        std::printf("[");
        for (unsigned k = 0; k < n; ++k)
          std::printf("%s%g", k ? ", " : "", f[k]);
        std::printf("]\n");
        break;
      }
      case aiPTI_Double: {
        unsigned n = prop->mDataLength / sizeof(double);
        const double* d = reinterpret_cast<const double*>(prop->mData);
        std::printf("[");
        for (unsigned k = 0; k < n; ++k)
          std::printf("%s%g", k ? ", " : "", d[k]);
        std::printf("]\n");
        break;
      }
      case aiPTI_Integer: {
        unsigned n = prop->mDataLength / sizeof(int32_t);
        const int32_t* v = reinterpret_cast<const int32_t*>(prop->mData);
        std::printf("[");
        for (unsigned k = 0; k < n; ++k)
          std::printf("%s%d", k ? ", " : "", v[k]);
        std::printf("]\n");
        break;
      }
      case aiPTI_String: {
        // assimp stores strings as a 4-byte length prefix followed by chars.
        if (prop->mDataLength >= sizeof(uint32_t) && prop->mData) {
          uint32_t len;
          std::memcpy(&len, prop->mData, sizeof(len));
          std::printf("\"%.*s\"\n", static_cast<int>(len),
                      prop->mData + sizeof(uint32_t));
        } else {
          std::printf("(empty)\n");
        }
        break;
      }
      case aiPTI_Buffer:
        std::printf("(%u bytes)\n", prop->mDataLength);
        break;
      default:
        std::printf("(unknown)\n");
        break;
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

  printMaterialProperties(mat);
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

  if (scene->mNumCameras > 0) {
    std::printf("\ncameras:\n");
    for (unsigned i = 0; i < scene->mNumCameras; ++i) {
      printCamera(scene->mCameras[i], i, parsed);
    }
  }

  return 0;
}
