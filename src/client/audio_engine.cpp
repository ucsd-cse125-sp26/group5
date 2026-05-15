#include "audio_engine.h"
#include <soloud.h>
#include <soloud_wav.h>
#include "shared/sound_constants.h"
#include "shared/util.h"
#include <cmath>
#include "shared/components.h"

bool AudioEngine::init() {
    soloud_ = new SoLoud::Soloud();
    
    printf("AudioEngine: attempting init...\n");
    
    SoLoud::result result = soloud_->init();
    
    // printf("AudioEngine: init returned %d\n", result);
    // printf("AudioEngine: backend: %s\n", soloud_->getBackendString());
    // printf("AudioEngine: sample rate: %d\n", soloud_->getBackendSamplerate());
    // printf("AudioEngine: buffer size: %d\n", soloud_->getBackendBufferSize());
    // printf("AudioEngine: channels: %d\n", soloud_->getBackendChannels());
    
    if (result != SoLoud::SO_NO_ERROR) {
        printf("AudioEngine: SoLoud init failed: %d\n", result);
        return false;
    }
    loadSound(static_cast<uint32_t>(shared::SoundId::JUMP),
              "assets/sounds/oof.mp3");
    loadSound(static_cast<uint32_t>(shared::SoundId::AMBIENT_HUM),
              "assets/sounds/scattered.wav");
    // loadSound(static_cast<uint32_t>(shared::SoundId::ENTITY_SPEAK),
    //           "assets/sounds/scattered.wav");
    return true;
}

void AudioEngine::shutdown() {
    if (soloud_) {
        soloud_->deinit();
        delete soloud_;
        soloud_ = nullptr;
    }
    for (auto& [id, wav] : sounds_) delete wav;
    sounds_.clear();
}

void AudioEngine::update() {
    // SoLoud handles mixing internally, but 3D audio needs this call
    soloud_->update3dAudio();
}

void AudioEngine::playSound(uint32_t soundId, float x, float y, float z,
                             float volume) {
    auto it = sounds_.find(soundId);
    if (it == sounds_.end()) return;
    SoLoud::handle h = soloud_->play3d(*it->second, x, y, z);
    soloud_->setVolume(h, volume);
}

void AudioEngine::setListenerPosition(float x, float y, float z,
                                       float forwardX, float forwardY,
                                       float forwardZ) {
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
                                 const shared::SoundEmitter& emitter,
                                 float x, float y, float z,
                                 float lx, float ly, float lz) {
    float dist = std::sqrt((x-lx)*(x-lx) + (y-ly)*(y-ly) + (z-lz)*(z-lz));

    printf("updateEmitter: entity=%u pos=(%.1f,%.1f,%.1f) listener=(%.1f,%.1f,%.1f) dist=%.1f\n",
        entityId, x, y, z, lx, ly, lz, dist);

    for (uint8_t i = 0; i < emitter.layerCount; i++) {
        const auto& layer = emitter.layers[i];
        printf("  layer %d: soundId=%u trigger=%d proximityRange=%.1f\n",
            i, layer.soundId, (int)layer.trigger, layer.proximityRange);
        bool shouldPlay = false;

        if (layer.trigger == shared::SoundTriggerType::ALWAYS) {
            shouldPlay = true;
        } 
        else if (layer.trigger == shared::SoundTriggerType::PROXIMITY) {
            shouldPlay = dist <= layer.proximityRange;
        }

        if (shouldPlay && !isLayerActive(entityId, layer.soundId)) {
            startLayer(entityId, layer.soundId, x, y, z, layer.volume);
        } else if (!shouldPlay && isLayerActive(entityId, layer.soundId)) {
            stopLayer(entityId, layer.soundId);
        } else if (shouldPlay && isLayerActive(entityId, layer.soundId)) {
            auto& handles = activeHandles_[entityId];
            auto it = handles.find(layer.soundId);
            if (it != handles.end()) {
                soloud_->set3dSourcePosition(it->second, x, y, z);
            }
        }
    }
}

void AudioEngine::stopAllForEntity(uint32_t entityId) {
    auto it = activeHandles_.find(entityId);
    if (it == activeHandles_.end()) return;
    for (auto& [soundId, handle] : it->second) {
        soloud_->stop(handle);
    }
    activeHandles_.erase(it);
}

void AudioEngine::startLayer(uint32_t entityId, uint32_t soundId,
                              float x, float y, float z, float volume) {
    auto it = sounds_.find(soundId);
    if (it == sounds_.end()) return;
    it->second->setLooping(true);
    unsigned int h = soloud_->play3d(*it->second, x, y, z);
    soloud_->setVolume(h, volume);
    activeHandles_[entityId][soundId] = h;
}

void AudioEngine::stopLayer(uint32_t entityId, uint32_t soundId) {
    auto eit = activeHandles_.find(entityId);
    if (eit == activeHandles_.end()) return;
    auto sit = eit->second.find(soundId);
    if (sit == eit->second.end()) return;
    soloud_->stop(sit->second);
    eit->second.erase(sit);
}

bool AudioEngine::isLayerActive(uint32_t entityId, uint32_t soundId) const {
    auto eit = activeHandles_.find(entityId);
    if (eit == activeHandles_.end()) return false;
    return eit->second.count(soundId) > 0;
}