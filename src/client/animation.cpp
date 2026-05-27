#include "client/animation.h"

#include <assimp/scene.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <utility>

#include "shared/mesh_loader.h"

namespace {

inline glm::vec3 toGlm(const aiVector3D& v) { return {v.x, v.y, v.z}; }
inline glm::quat toGlm(const aiQuaternion& q) { return {q.w, q.x, q.y, q.z}; }
inline glm::mat4 toGlm(const aiMatrix4x4& m) {
  return glm::transpose(glm::make_mat4(&m.a1));
}

}  // namespace

// ─── Bone ──────────────────────────────────────────────────────────────────

Bone::Bone(std::string name, int id, const aiNodeAnim* channel)
    : name_(std::move(name)), id_(id) {
  positions_.reserve(channel->mNumPositionKeys);
  for (unsigned i = 0; i < channel->mNumPositionKeys; ++i) {
    positions_.push_back(
        {.position = toGlm(channel->mPositionKeys[i].mValue),
         .timeStamp = static_cast<float>(channel->mPositionKeys[i].mTime)});
  }
  rotations_.reserve(channel->mNumRotationKeys);
  for (unsigned i = 0; i < channel->mNumRotationKeys; ++i) {
    rotations_.push_back(
        {.orientation = toGlm(channel->mRotationKeys[i].mValue),
         .timeStamp = static_cast<float>(channel->mRotationKeys[i].mTime)});
  }
  scales_.reserve(channel->mNumScalingKeys);
  for (unsigned i = 0; i < channel->mNumScalingKeys; ++i) {
    scales_.push_back(
        {.scale = toGlm(channel->mScalingKeys[i].mValue),
         .timeStamp = static_cast<float>(channel->mScalingKeys[i].mTime)});
  }
}

void Bone::update(float t) {
  localTransform_ =
      interpolatePosition(t) * interpolateRotation(t) * interpolateScale(t);
}

float Bone::scaleFactor(float last, float next, float t) {
  float span = next - last;
  if (span <= 0.0f) return 0.0f;
  return (t - last) / span;
}

int Bone::positionIndex(float t) const {
  for (size_t i = 0; i + 1 < positions_.size(); ++i) {
    if (t < positions_[i + 1].timeStamp) return static_cast<int>(i);
  }
  return static_cast<int>(positions_.size()) - 1;
}

int Bone::rotationIndex(float t) const {
  for (size_t i = 0; i + 1 < rotations_.size(); ++i) {
    if (t < rotations_[i + 1].timeStamp) return static_cast<int>(i);
  }
  return static_cast<int>(rotations_.size()) - 1;
}

int Bone::scaleIndex(float t) const {
  for (size_t i = 0; i + 1 < scales_.size(); ++i) {
    if (t < scales_[i + 1].timeStamp) return static_cast<int>(i);
  }
  return static_cast<int>(scales_.size()) - 1;
}

glm::mat4 Bone::interpolatePosition(float t) const {
  if (positions_.empty()) return {1.0f};
  if (positions_.size() == 1)
    return glm::translate(glm::mat4(1.0f), positions_[0].position);
  int p0 = positionIndex(t);
  int p1 = std::min<int>(p0 + 1, static_cast<int>(positions_.size()) - 1);
  float f = scaleFactor(positions_[p0].timeStamp, positions_[p1].timeStamp, t);
  glm::vec3 v = glm::mix(positions_[p0].position, positions_[p1].position, f);
  return glm::translate(glm::mat4(1.0f), v);
}

glm::mat4 Bone::interpolateRotation(float t) const {
  if (rotations_.empty()) return {1.0f};
  if (rotations_.size() == 1)
    return glm::mat4_cast(glm::normalize(rotations_[0].orientation));
  int p0 = rotationIndex(t);
  int p1 = std::min<int>(p0 + 1, static_cast<int>(rotations_.size()) - 1);
  float f = scaleFactor(rotations_[p0].timeStamp, rotations_[p1].timeStamp, t);
  glm::quat q =
      glm::slerp(rotations_[p0].orientation, rotations_[p1].orientation, f);
  return glm::mat4_cast(glm::normalize(q));
}

glm::mat4 Bone::interpolateScale(float t) const {
  if (scales_.empty()) return {1.0f};
  if (scales_.size() == 1) return glm::scale(glm::mat4(1.0f), scales_[0].scale);
  int p0 = scaleIndex(t);
  int p1 = std::min<int>(p0 + 1, static_cast<int>(scales_.size()) - 1);
  float f = scaleFactor(scales_[p0].timeStamp, scales_[p1].timeStamp, t);
  glm::vec3 v = glm::mix(scales_[p0].scale, scales_[p1].scale, f);
  return glm::scale(glm::mat4(1.0f), v);
}

// ─── Animation ─────────────────────────────────────────────────────────────

Animation::Animation(const aiScene* scene, unsigned animIndex, Model* model) {
  assert(scene && animIndex < scene->mNumAnimations);
  const aiAnimation* anim = scene->mAnimations[animIndex];
  duration_ = static_cast<float>(anim->mDuration);
  ticksPerSecond_ = anim->mTicksPerSecond > 0.0
                        ? static_cast<float>(anim->mTicksPerSecond)
                        : 25.0f;
  readHierarchy(rootNode_, scene->mRootNode);
  readMissingBones(anim, model);
}

void Animation::readHierarchy(AssimpNodeData& dest, const aiNode* src) {
  dest.name = src->mName.C_Str();
  dest.transformation = toGlm(src->mTransformation);
  dest.children.reserve(src->mNumChildren);
  for (unsigned i = 0; i < src->mNumChildren; ++i) {
    AssimpNodeData child;
    readHierarchy(child, src->mChildren[i]);
    dest.children.push_back(std::move(child));
  }
}

void Animation::readMissingBones(const aiAnimation* anim, Model* model) {
  for (unsigned i = 0; i < anim->mNumChannels; ++i) {
    const aiNodeAnim* channel = anim->mChannels[i];
    std::string name = channel->mNodeName.C_Str();
    auto it = model->boneInfoMap.find(name);
    if (it == model->boneInfoMap.end()) {
      // Some bones drive transforms but don't directly skin vertices.
      BoneInfo info;
      info.id = model->boneCount++;
      info.offset = glm::mat4(1.0f);
      model->boneInfoMap[name] = info;
      it = model->boneInfoMap.find(name);
    }
    bones_.emplace_back(name, it->second.id, channel);
  }
  boneInfoMap_ = model->boneInfoMap;
}

Bone* Animation::findBone(const std::string& name) {
  auto it = std::ranges::find_if(
      bones_, [&](const Bone& b) { return b.name() == name; });
  return it == bones_.end() ? nullptr : &*it;
}

// ─── Animator ──────────────────────────────────────────────────────────────

Animator::Animator() : finalBoneMatrices_(MAX_BONES, glm::mat4(1.0f)) {}

void Animator::play(Animation* anim) {
  current_ = anim;
  currentTime_ = 0.0f;
}

void Animator::update(float dt) {
  if (!current_) return;
  currentTime_ += current_->ticksPerSecond() * dt;
  if (current_->duration() > 0.0f) {
    currentTime_ = std::fmod(currentTime_, current_->duration());
  }
  calculateBoneTransform(&current_->rootNode(), glm::mat4(1.0f));
}

void Animator::calculateBoneTransform(const AssimpNodeData* node,
                                      const glm::mat4& parentTransform) {
  glm::mat4 nodeTransform = node->transformation;
  if (Bone* bone = current_->findBone(node->name)) {
    bone->update(currentTime_);
    nodeTransform = bone->localTransform();
  }
  // Apply any per-node override (e.g., look pitch) in the bone's local
  // space. Descendants inherit it via parentTransform on the recursion.
  auto over = boneOverrides_.find(node->name);
  if (over != boneOverrides_.end()) {
    nodeTransform = nodeTransform * over->second;
  }
  glm::mat4 globalTransform = parentTransform * nodeTransform;

  const auto& boneMap = current_->boneInfoMap();
  auto it = boneMap.find(node->name);
  if (it != boneMap.end() && it->second.id < MAX_BONES) {
    finalBoneMatrices_[it->second.id] = globalTransform * it->second.offset;
  }
  for (const auto& child : node->children) {
    calculateBoneTransform(&child, globalTransform);
  }
}

// ─── AnimationLibrary ──────────────────────────────────────────────────────

AnimationLibrary::AnimationLibrary(Model* model) {
  if (!model || !model->parsed) return;
  const aiScene* scene = model->parsed->scene();
  if (!scene) return;
  clips_.reserve(scene->mNumAnimations);
  for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
    auto anim = std::make_unique<Animation>(scene, i, model);
    byName_[scene->mAnimations[i]->mName.C_Str()] = anim.get();
    clips_.push_back(std::move(anim));
  }
}

Animation* AnimationLibrary::find(const std::string& clipName,
                                  bool fallbackToFirst) {
  auto it = byName_.find(clipName);
  if (it != byName_.end()) return it->second;
  if (fallbackToFirst && !clips_.empty()) return clips_.front().get();
  return nullptr;
}
