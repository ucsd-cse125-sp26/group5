#pragma once

#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace shared {

class ParsedModel {
 public:
  bool load(const std::string& path, unsigned int aiFlags);

  bool isValid() const { return scene_ != nullptr; }
  const aiScene* scene() const { return scene_; }
  const std::string& path() const { return path_; }
  const std::string& lastError() const { return error_; }

  const aiMatrix4x4* worldTransform(const aiNode* node) const;

  // Resolves via aiScene::FindNode — returns the first DFS match. glTF
  // allows duplicate node names; prefer the aiNode* overload when iterating.
  const aiMatrix4x4* worldTransform(const std::string& name) const;

  // Iteration order is unspecified.
  template <typename Fn>
  void forEachMeshNode(Fn fn) const {
    for (const auto& [node, mat] : worldTransforms_) {
      if (node->mNumMeshes > 0) fn(*node, mat);
    }
  }

 private:
  void collect_(const aiNode* node, const aiMatrix4x4& parent);

  Assimp::Importer importer_;
  const aiScene* scene_ = nullptr;
  std::string path_;
  std::string error_;
  // Keyed by aiNode* so duplicate or empty node names (both legal in glTF)
  // don't collapse into a single transform.
  std::unordered_map<const aiNode*, aiMatrix4x4> worldTransforms_;
};

// Positions are in mesh-local space (no node transform applied).
void flattenNodeGeometry(const aiNode& node, const aiScene& scene,
                         std::vector<aiVector3D>& outPositions,
                         std::vector<uint32_t>& outIndices);

}  // namespace shared
