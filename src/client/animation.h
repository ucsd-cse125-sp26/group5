#pragma once

#include <assimp/scene.h>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "client/asset.h"

struct KeyPosition {
  glm::vec3 position;
  float timeStamp;
};

struct KeyRotation {
  glm::quat orientation;
  float timeStamp;
};

struct KeyScale {
  glm::vec3 scale;
  float timeStamp;
};

class Bone {
 public:
  Bone(std::string name, int id, const aiNodeAnim* channel);

  void update(float animationTime);
  const glm::mat4& localTransform() const { return localTransform_; }
  const std::string& name() const { return name_; }
  int id() const { return id_; }

 private:
  int positionIndex(float t) const;
  int rotationIndex(float t) const;
  int scaleIndex(float t) const;
  static float scaleFactor(float last, float next, float t);
  glm::mat4 interpolatePosition(float t) const;
  glm::mat4 interpolateRotation(float t) const;
  glm::mat4 interpolateScale(float t) const;

  std::vector<KeyPosition> positions_;
  std::vector<KeyRotation> rotations_;
  std::vector<KeyScale> scales_;
  glm::mat4 localTransform_{1.0f};
  std::string name_;
  int id_;
};

struct AssimpNodeData {
  glm::mat4 transformation{1.0f};
  std::string name;
  std::vector<AssimpNodeData> children;
};

// Reads one aiAnimation. `model` is borrowed to extend its boneInfoMap with
// channels that name bones the mesh didn't declare (common in DAE exports).
class Animation {
 public:
  Animation(const aiScene* scene, unsigned animIndex, Model* model);

  Bone* findBone(const std::string& name);
  float ticksPerSecond() const { return ticksPerSecond_; }
  float duration() const { return duration_; }
  const AssimpNodeData& rootNode() const { return rootNode_; }
  const std::unordered_map<std::string, BoneInfo>& boneInfoMap() const {
    return boneInfoMap_;
  }

 private:
  void readHierarchy(AssimpNodeData& dest, const aiNode* src);
  void readMissingBones(const aiAnimation* anim, Model* model);

  float duration_ = 0.0f;
  float ticksPerSecond_ = 25.0f;
  std::vector<Bone> bones_;
  AssimpNodeData rootNode_;
  std::unordered_map<std::string, BoneInfo> boneInfoMap_;
};

class Animator {
 public:
  Animator();

  void play(Animation* anim);
  void update(float dt);

  // Post-multiplies the named node's local transform during the next
  // update(). Use for cheap "look pitch" / head-track effects without
  // touching keyframes. Caller is expected to refresh overrides each frame
  // (or call clearBoneOverrides()) since they otherwise persist.
  void setBoneOverride(const std::string& nodeName, const glm::mat4& m) {
    boneOverrides_[nodeName] = m;
  }
  void clearBoneOverrides() { boneOverrides_.clear(); }

  const std::vector<glm::mat4>& finalBoneMatrices() const {
    return finalBoneMatrices_;
  }
  Animation* current() const { return current_; }

 private:
  void calculateBoneTransform(const AssimpNodeData* node,
                              const glm::mat4& parentTransform);

  std::vector<glm::mat4> finalBoneMatrices_;
  std::unordered_map<std::string, glm::mat4> boneOverrides_;
  Animation* current_ = nullptr;
  float currentTime_ = 0.0f;
};

// Builds and owns Animations for a Model's parsed aiScene.
class AnimationLibrary {
 public:
  explicit AnimationLibrary(Model* model);

  // Looks up by aiAnimation::mName; falls back to first clip if name empty
  // or not found and `fallbackToFirst` is true. Returns nullptr otherwise.
  Animation* find(const std::string& clipName, bool fallbackToFirst = true);

  bool empty() const { return clips_.empty(); }

 private:
  std::vector<std::unique_ptr<Animation>> clips_;
  std::unordered_map<std::string, Animation*> byName_;
};
