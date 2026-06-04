#include "audio_engine.h"

#include <soloud.h>
#include <soloud_wav.h>

#include <algorithm>
#include <cmath>

#include "shared/components.h"
#include "shared/sound_constants.h"
#include "shared/util.h"

bool AudioEngine::init() {
  std::scoped_lock lock(mutex_);
  soloud_ = new SoLoud::Soloud();
  printf("AudioEngine: attempting init...\n");

  SoLoud::result result = soloud_->init();

  if (result != SoLoud::SO_NO_ERROR) {
    printf(
        "AudioEngine: SoLoud init failed (error %d), retrying with null "
        "driver\n",
        result);
    result = soloud_->init(SoLoud::Soloud::CLIP_ROUNDOFF,
                           SoLoud::Soloud::NULLDRIVER);
    if (result != SoLoud::SO_NO_ERROR) {
      printf("AudioEngine: null driver also failed: %d\n", result);
      return false;
    }
    printf("AudioEngine: running in silent mode (no audio output)\n");
  }

  // raise voice limit to 32 for more simultaneous sounds
  soloud_->setMaxActiveVoiceCount(32);

  loadSound(static_cast<uint32_t>(shared::SoundId::JUMP),
            "assets/sounds/oof.mp3");
  loadSound(static_cast<uint32_t>(shared::SoundId::AMBIENT_HUM),
            "assets/sounds/scattered.wav");
  // uncomment when you have the files:
  loadSound(static_cast<uint32_t>(shared::SoundId::OVERWORLD_MUSIC),
            "assets/sounds/angel.mp3");
  loadSound(static_cast<uint32_t>(shared::SoundId::MAZE_MUSIC),
            "assets/sounds/yaku.mp3");
  // Placeholder credits track — swap for a dedicated file when available.
  loadSound(static_cast<uint32_t>(shared::SoundId::CREDITS_MUSIC),
            "assets/sounds/angel.mp3");
  loadSound(static_cast<uint32_t>(shared::SoundId::LAND),
            "assets/sounds/oof.mp3");  // temporary, reuses jump sound

  // in AudioEngine::init(), around the other puzzle sound loadSound calls:
  loadSound(static_cast<uint32_t>(shared::SoundId::PUZZLE_SOLVED),
            "assets/sounds/angel.mp3");
  // loadSound(static_cast<uint32_t>(shared::SoundId::FOOTSTEP_1),
  // "assets/sounds/oof.mp3");
  // loadSound(static_cast<uint32_t>(shared::SoundId::FOOTSTEP_2),
  // "assets/sounds/oof.mp3");
  // loadSound(static_cast<uint32_t>(shared::SoundId::FOOTSTEP_3),
  // "assets/sounds/footstep_3.wav");
  // loadSound(static_cast<uint32_t>(shared::SoundId::FOOTSTEP_4),
  // "assets/sounds/footstep_4.wav"); Section ambients — replace paths with your
  // actual files
  // loadSound(static_cast<uint32_t>(shared::SoundId::SECTION_WINTER_AMBIENT),
  //           "assets/sounds/winter_ambient.wav");
  // loadSound(static_cast<uint32_t>(shared::SoundId::SECTION_FALL_AMBIENT),
  //           "assets/sounds/fall_ambient.wav");
  // loadSound(static_cast<uint32_t>(shared::SoundId::SECTION_SUMMER_AMBIENT),
  //           "assets/sounds/summer_ambient.wav");
  // loadSound(static_cast<uint32_t>(shared::SoundId::SECTION_SPRING_AMBIENT),
  //           "assets/sounds/spring_ambient.wav");
  // // Puzzle sounds
  // loadSound(static_cast<uint32_t>(shared::SoundId::PUZZLE_SWITCH_FLIP),
  //           "assets/sounds/switch.wav");
  // loadSound(static_cast<uint32_t>(shared::SoundId::PUZZLE_DOOR_OPEN),
  //           "assets/sounds/door.wav");
  // loadSound(static_cast<uint32_t>(shared::SoundId::PUZZLE_SOLVED),
  //           "assets/sounds/solved.wav");
  // loadSound(static_cast<uint32_t>(shared::SoundId::PUZZLE_FAILED),
  //           "assets/sounds/failed.wav");
  return true;
}

void AudioEngine::shutdown() {
  std::scoped_lock lock(mutex_);
  if (soloud_) {
    soloud_->deinit();
    delete soloud_;
    soloud_ = nullptr;
  }
  for (auto& [id, wav] : sounds_) delete wav;
  sounds_.clear();
}

void AudioEngine::update(float dt) {
  std::scoped_lock lock(mutex_);
  soloud_->update3dAudio();
}

void AudioEngine::setMasterVolume(float volume) {
  std::scoped_lock lock(mutex_);
  masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
  soloud_->setGlobalVolume(masterVolume_);
}

void AudioEngine::playSound(uint32_t soundId, float x, float y, float z,
                            float volume, float pitch) {
  std::scoped_lock lock(mutex_);
  auto it = sounds_.find(soundId);
  if (it == sounds_.end()) return;
  SoLoud::handle h = soloud_->play3d(*it->second, x, y, z);
  soloud_->setVolume(h, volume);
  soloud_->setRelativePlaySpeed(h, pitch);
}

void AudioEngine::playNonPositionalSound(uint32_t soundId, float volume,
                                         float pitch) {
  std::scoped_lock lock(mutex_);
  auto it = sounds_.find(soundId);
  if (it == sounds_.end()) return;
  SoLoud::handle h = soloud_->play(*it->second);
  soloud_->setVolume(h, volume);
  soloud_->setRelativePlaySpeed(h, pitch);
}

void AudioEngine::setListenerPosition(float x, float y, float z, float forwardX,
                                      float forwardY, float forwardZ) {
  std::scoped_lock lock(mutex_);
  soloud_->set3dListenerPosition(x, y, z);
  soloud_->set3dListenerAt(forwardX, forwardY, forwardZ);
  soloud_->set3dListenerUp(0.0f, 0.0f, 1.0f);
}

void AudioEngine::loadSound(uint32_t soundId, const std::string& path) {
  auto* wav = new SoLoud::Wav();
  std::string fullPath = (exeDir() / path).string();
  SoLoud::result result = wav->load(fullPath.c_str());
  if (result != SoLoud::SO_NO_ERROR) {
    printf("AudioEngine: failed to load sound %s\n", fullPath.c_str());
    delete wav;
    return;
  }
  wav->set3dMinMaxDistance(5.0f, 1000.0f);
  wav->set3dAttenuation(SoLoud::AudioSource::INVERSE_DISTANCE, 1.0f);
  sounds_[soundId] = wav;
}

void AudioEngine::updateEmitter(uint32_t entityId,
                                const shared::SoundEmitter& emitter, float x,
                                float y, float z, float lx, float ly, float lz,
                                float dt) {
  std::scoped_lock lock(mutex_);
  float dist = std::sqrt((x - lx) * (x - lx) + (y - ly) * (y - ly) +
                         (z - lz) * (z - lz));

  for (uint8_t i = 0; i < emitter.layerCount; i++) {
    const auto& layer = emitter.layers[i];

    if (layer.trigger == shared::SoundTriggerType::ON_EVENT) continue;

    // start the layer if not already running — starts silent, fade handles
    // volume
    if (!isLayerActive(entityId, layer.soundId)) {
      startLayer(entityId, layer.soundId, x, y, z, layer.playMode);
    }

    // calculate target volume based on trigger type
    float targetVolume = 0.0f;
    if (layer.trigger == shared::SoundTriggerType::ALWAYS) {
      targetVolume = layer.volume;
    } else if (layer.trigger == shared::SoundTriggerType::PROXIMITY) {
      if (dist < layer.proximityRange) {
        // full volume at center, fades to 0 at edge
        float t = 1.0f - (dist / layer.proximityRange);
        targetVolume = layer.volume * std::clamp(t, 0.0f, 1.0f);
      }
    }

    // lerp current volume toward target
    float& currentVol = layerVolumes_[entityId][layer.soundId];
    float delta = layer.fadeSpeed * dt;
    if (currentVol < targetVolume) {
      currentVol = std::min(currentVol + delta, targetVolume);
    } else {
      currentVol = std::max(currentVol - delta, targetVolume);
    }

    // apply volume and update position
    auto& handles = activeHandles_[entityId];
    auto it = handles.find(layer.soundId);
    if (it != handles.end()) {
      soloud_->setVolume(it->second, currentVol);
      if (layer.playMode == shared::SoundPlayMode::POSITIONAL) {
        soloud_->set3dSourcePosition(it->second, x, y, z);
      }
    }
  }
}

void AudioEngine::stopAllForEntity(uint32_t entityId) {
  std::scoped_lock lock(mutex_);
  auto it = activeHandles_.find(entityId);
  if (it == activeHandles_.end()) return;
  for (auto& [soundId, handle] : it->second) {
    soloud_->stop(handle);
  }
  activeHandles_.erase(it);
  layerVolumes_.erase(entityId);
}

void AudioEngine::startLayer(uint32_t entityId, uint32_t soundId, float x,
                             float y, float z, shared::SoundPlayMode mode) {
  auto it = sounds_.find(soundId);
  if (it == sounds_.end()) return;
  it->second->setLooping(true);
  unsigned int h;
  if (mode == shared::SoundPlayMode::AMBIENT) {
    h = soloud_->play(*it->second);
  } else {
    h = soloud_->play3d(*it->second, x, y, z);
  }
  soloud_->setVolume(h, 0.0f);  // start silent, fade in
  activeHandles_[entityId][soundId] = h;
  layerVolumes_[entityId][soundId] = 0.0f;
}

void AudioEngine::stopLayer(uint32_t entityId, uint32_t soundId) {
  auto eit = activeHandles_.find(entityId);
  if (eit == activeHandles_.end()) return;
  auto sit = eit->second.find(soundId);
  if (sit == eit->second.end()) return;
  soloud_->stop(sit->second);
  eit->second.erase(sit);
  if (layerVolumes_.count(entityId)) layerVolumes_[entityId].erase(soundId);
}

bool AudioEngine::isLayerActive(uint32_t entityId, uint32_t soundId) const {
  auto eit = activeHandles_.find(entityId);
  if (eit == activeHandles_.end()) return false;
  return eit->second.count(soundId) > 0;
}

void AudioEngine::playGlobalLoop(uint32_t soundId, float volume) {
  std::scoped_lock lock(mutex_);
  auto it = sounds_.find(soundId);
  if (it == sounds_.end()) return;
  it->second->setLooping(true);
  unsigned int h = soloud_->play(*it->second);
  soloud_->setVolume(h, volume);
  globalHandles_[soundId] = h;
}

void AudioEngine::stopGlobalLoop(uint32_t soundId) {
  std::scoped_lock lock(mutex_);
  auto it = globalHandles_.find(soundId);
  if (it == globalHandles_.end()) return;
  soloud_->stop(it->second);
  globalHandles_.erase(it);
}

void AudioEngine::stopAllGlobalLoops() {
  std::scoped_lock lock(mutex_);
  for (auto& [soundId, handle] : globalHandles_) {
    soloud_->stop(handle);
  }
  globalHandles_.clear();
}