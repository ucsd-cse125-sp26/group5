#include "shared/mesh_loader.h"

namespace shared {

bool ParsedModel::load(const std::string& path, unsigned int aiFlags) {
  worldTransforms_.clear();
  scene_ = nullptr;
  path_ = path;
  error_.clear();

  scene_ = importer_.ReadFile(path, aiFlags);
  if (!scene_ || (scene_->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ||
      !scene_->mRootNode) {
    error_ = importer_.GetErrorString();
    scene_ = nullptr;
    return false;
  }
  collect_(scene_->mRootNode, aiMatrix4x4());
  return true;
}

const aiMatrix4x4* ParsedModel::worldTransform(const aiNode* node) const {
  auto it = worldTransforms_.find(node);
  return it == worldTransforms_.end() ? nullptr : &it->second;
}

const aiMatrix4x4* ParsedModel::worldTransform(const std::string& name) const {
  if (!scene_ || !scene_->mRootNode) return nullptr;
  const aiNode* node = scene_->mRootNode->FindNode(aiString(name));
  return node ? worldTransform(node) : nullptr;
}

void ParsedModel::collect_(const aiNode* node, const aiMatrix4x4& parent) {
  aiMatrix4x4 world = parent * node->mTransformation;
  worldTransforms_[node] = world;
  for (unsigned i = 0; i < node->mNumChildren; ++i) {
    collect_(node->mChildren[i], world);
  }
}

void flattenNodeGeometry(const aiNode& node, const aiScene& scene,
                         std::vector<aiVector3D>& outPositions,
                         std::vector<uint32_t>& outIndices) {
  for (unsigned i = 0; i < node.mNumMeshes; ++i) {
    const aiMesh* mesh = scene.mMeshes[node.mMeshes[i]];
    auto base = static_cast<uint32_t>(outPositions.size());
    outPositions.insert(outPositions.end(), mesh->mVertices,
                        mesh->mVertices + mesh->mNumVertices);
    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      if (face.mNumIndices != 3) continue;  // post-triangulate guarantee
      outIndices.push_back(base + face.mIndices[0]);
      outIndices.push_back(base + face.mIndices[1]);
      outIndices.push_back(base + face.mIndices[2]);
    }
  }
}

}  // namespace shared
