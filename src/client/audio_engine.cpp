#include "audio_engine.h"

#include <soloud.h>
#include <soloud_wav.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "shared/components.h"
#include "shared/log.h"
#include "shared/sound_constants.h"
#include "shared/util.h"

namespace {

#if defined(_WIN32)
constexpr const char* kAudioPlatform = "Windows";
constexpr const char* kPrimaryAudioBackend = "MINIAUDIO/WASAPI";
#elif defined(__APPLE__)
constexpr const char* kAudioPlatform = "macOS";
constexpr const char* kPrimaryAudioBackend = "COREAUDIO";
#elif defined(__linux__)
constexpr const char* kAudioPlatform = "Linux";
constexpr const char* kPrimaryAudioBackend = "MINIAUDIO/ALSA-Pulse-PipeWire";
#else
constexpr const char* kAudioPlatform = "Unknown";
constexpr const char* kPrimaryAudioBackend = "SoLoud AUTO";
#endif

}  // namespace

bool AudioEngine::init() {
  std::scoped_lock lock(mutex_);
  soloud_ = new SoLoud::Soloud();
  std::fprintf(stderr, "AudioEngine: init platform=%s backend=%s\n",
               kAudioPlatform, kPrimaryAudioBackend);

  SoLoud::result result = SoLoud::UNKNOWN_ERROR;
  bool usingSilentDriver = false;
#if defined(__APPLE__)
  // Prefer native Core Audio on macOS (miniaudio often fails with error 7).
  result =
      soloud_->init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::COREAUDIO);
  if (result != SoLoud::SO_NO_ERROR) {
    std::fprintf(stderr,
                 "AudioEngine: CoreAudio failed (%d: %s), trying miniaudio\n",
                 result, soloud_->getErrorString(result));
    result = soloud_->init(SoLoud::Soloud::CLIP_ROUNDOFF,
                           SoLoud::Soloud::MINIAUDIO, 48000, 1024, 2);
  }
#elif defined(_WIN32) || defined(__linux__)
  result = soloud_->init(SoLoud::Soloud::CLIP_ROUNDOFF,
                         SoLoud::Soloud::MINIAUDIO, 48000, 1024, 2);
#else
  result = soloud_->init();
#endif

  if (result != SoLoud::SO_NO_ERROR) {
    std::fprintf(
        stderr,
        "AudioEngine ERROR: real audio backend failed on %s (%d: %s). "
        "Trying NULLDRIVER; music will be silent if this succeeds.\n",
        kAudioPlatform, result, soloud_->getErrorString(result));
    result = soloud_->init(SoLoud::Soloud::CLIP_ROUNDOFF,
                           SoLoud::Soloud::NULLDRIVER);
    if (result != SoLoud::SO_NO_ERROR) {
      std::fprintf(stderr, "AudioEngine ERROR: NULLDRIVER also failed: %d\n",
                   result);
      return false;
    }
    usingSilentDriver = true;
  }
  std::fprintf(stderr, "AudioEngine: backend active=%s samplerate=%u%s\n",
               soloud_->getBackendString(), soloud_->getBackendSamplerate(),
               usingSilentDriver ? " (SILENT NULLDRIVER)" : "");

  // raise voice limit to 32 for more simultaneous sounds
  soloud_->setMaxActiveVoiceCount(32);

  // Temporarily disable non-seasonal audio while validating seasonal music.
  // loadSound(static_cast<uint32_t>(shared::SoundId::AMBIENT_HUM),
  //           "assets/sounds/scattered.wav");
  // loadSound(static_cast<uint32_t>(shared::SoundId::MAZE_MUSIC),
  //           "assets/sounds/yaku.mp3");
  // loadSound(static_cast<uint32_t>(shared::SoundId::PUZZLE_SOLVED),
  //           "assets/sounds/minigame_complete.wav");
  loadSound(static_cast<uint32_t>(shared::SoundId::SECTION_WINTER_AMBIENT),
            "assets/sounds/Winter.mp3");
  loadSound(static_cast<uint32_t>(shared::SoundId::SECTION_FALL_AMBIENT),
            "assets/sounds/Fall.mp3");
  loadSound(static_cast<uint32_t>(shared::SoundId::SECTION_SUMMER_AMBIENT),
            "assets/sounds/Summer.mp3");
  loadSound(static_cast<uint32_t>(shared::SoundId::SECTION_SPRING_AMBIENT),
            "assets/sounds/Spring.mp3");
  loadSound(static_cast<uint32_t>(shared::SoundId::SECTION_AFTER_SPRING_AMBIENT),
            "assets/sounds/AfterSpring.mp3");
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

  if (globalMusicHandle_ != 0 &&
      globalMusicVolume_ < globalMusicTargetVolume_) {
    const float fadeStep =
        (globalMusicTargetVolume_ / kGlobalMusicFadeSeconds) * dt;
    globalMusicVolume_ =
        std::min(globalMusicVolume_ + fadeStep, globalMusicTargetVolume_);
    soloud_->setVolume(globalMusicHandle_, globalMusicVolume_);
  }
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
    LOG_DEBUG("AudioEngine: failed to load sound %s\n", fullPath.c_str());
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

    if (!isLayerActive(entityId, layer.soundId)) {
      startLayer(entityId, layer.soundId, x, y, z, layer.playMode);
    }

    float targetVolume = 0.0f;
    if (layer.trigger == shared::SoundTriggerType::ALWAYS) {
      targetVolume = layer.volume;
    } else if (layer.trigger == shared::SoundTriggerType::PROXIMITY) {
      if (dist < layer.proximityRange) {
        float t = 1.0f - (dist / layer.proximityRange);
        targetVolume = layer.volume * std::clamp(t, 0.0f, 1.0f);
      }
    }

    float& currentVol = layerVolumes_[entityId][layer.soundId];
    float delta = layer.fadeSpeed * dt;
    if (currentVol < targetVolume) {
      currentVol = std::min(currentVol + delta, targetVolume);
    } else {
      currentVol = std::max(currentVol - delta, targetVolume);
    }

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
  soloud_->setVolume(h, 0.0f);
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
  if (globalMusicSoundId_ == soundId && globalMusicHandle_ != 0) {
    globalMusicTargetVolume_ = volume;
    return;
  }

  auto soundIt = sounds_.find(soundId);
  if (soundIt == sounds_.end()) {
    std::fprintf(stderr,
                 "AudioEngine: global loop sound id %u not loaded "
                 "(missing assets/sounds?)\n",
                 soundId);
    return;
  }

  if (globalMusicHandle_ != 0) {
    soloud_->stop(globalMusicHandle_);
    globalMusicHandle_ = 0;
    globalMusicSoundId_ = 0;
    globalMusicVolume_ = 0.0f;
  }
  if (globalMusicFadeOutHandle_ != 0) {
    soloud_->stop(globalMusicFadeOutHandle_);
    globalMusicFadeOutHandle_ = 0;
    globalMusicFadeOutVolume_ = 0.0f;
  }

  const char* sourceType = "buffered sound";
  soundIt->second->setLooping(true);
  globalMusicHandle_ = soloud_->play(*soundIt->second);
  std::fprintf(stderr,
               "AudioEngine: starting global music id=%u source=%s "
               "handle=%u targetVolume=%.2f\n",
               soundId, sourceType, globalMusicHandle_, volume);
  globalMusicSoundId_ = soundId;
  globalMusicTargetVolume_ = volume;
  globalMusicVolume_ = 0.0f;
  soloud_->setVolume(globalMusicHandle_, 0.0f);

}

void AudioEngine::stopGlobalLoop(uint32_t soundId) {
  std::scoped_lock lock(mutex_);
  if (globalMusicSoundId_ == soundId && globalMusicHandle_ != 0) {
    soloud_->stop(globalMusicHandle_);
    globalMusicHandle_ = 0;
    globalMusicSoundId_ = 0;
    globalMusicVolume_ = 0.0f;
    globalMusicTargetVolume_ = 0.0f;
    return;
  }
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
  if (globalMusicFadeOutHandle_ != 0) {
    soloud_->stop(globalMusicFadeOutHandle_);
    globalMusicFadeOutHandle_ = 0;
  }
  if (globalMusicHandle_ != 0) {
    soloud_->stop(globalMusicHandle_);
    globalMusicHandle_ = 0;
  }
  globalMusicSoundId_ = 0;
  globalMusicVolume_ = 0.0f;
  globalMusicTargetVolume_ = 0.0f;
  globalMusicFadeOutVolume_ = 0.0f;
}
