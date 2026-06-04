#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "shared/components.h"

namespace SoLoud {
class Soloud;
class Wav;
}  // namespace SoLoud
namespace shared {
struct SoundEmitter;
}

class AudioEngine {
 public:
  bool init();
  void shutdown();
  void update(float dt);  // dt added for fade

  void playSound(uint32_t soundId, float x, float y, float z,
                 float volume = 1.0f, float pitch = 1.0f);

  void updateEmitter(uint32_t entityId, const shared::SoundEmitter& emitter,
                     float x, float y, float z, float listenerX,
                     float listenerY, float listenerZ,
                     float dt);  // dt added for fade

  void stopAllForEntity(uint32_t entityId);
  void setListenerPosition(float x, float y, float z, float forwardX,
                           float forwardY, float forwardZ);

  // Global non-positional loops (crossfades when switching tracks).
  void playGlobalLoop(uint32_t soundId, float volume);
  void stopGlobalLoop(uint32_t soundId);
  void stopAllGlobalLoops();

  // One-shot non-positional for puzzle events
  void playNonPositionalSound(uint32_t soundId, float volume = 1.0f,
                              float pitch = 1.0f);

  // Master volume: 0.0 to 1.0
  void setMasterVolume(float volume);
  float getMasterVolume() const { return masterVolume_; }

 private:
  SoLoud::Soloud* soloud_ = nullptr;
  std::unordered_map<uint32_t, SoLoud::Wav*> sounds_;
  float masterVolume_ = 1.0f;

  // entityId → (soundId → SoLoud handle)
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, unsigned int>>
      activeHandles_;

  // entityId → (soundId → current faded volume)
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, float>>
      layerVolumes_;

  // soundId → SoLoud handle for global loops (legacy one-shots)
  std::unordered_map<uint32_t, unsigned int> globalHandles_;

  // Crossfaded overworld / maze background music
  unsigned int globalMusicHandle_ = 0;
  unsigned int globalMusicFadeOutHandle_ = 0;
  uint32_t globalMusicSoundId_ = 0;
  float globalMusicVolume_ = 0.0f;
  float globalMusicTargetVolume_ = 0.0f;
  float globalMusicFadeOutVolume_ = 0.0f;
  static constexpr float kGlobalMusicFadeSpeed = 0.35f;

  void loadSound(uint32_t soundId, const std::string& path);
  void startLayer(uint32_t entityId, uint32_t soundId, float x, float y,
                  float z, shared::SoundPlayMode mode);
  void stopLayer(uint32_t entityId, uint32_t soundId);
  bool isLayerActive(uint32_t entityId, uint32_t soundId) const;
};