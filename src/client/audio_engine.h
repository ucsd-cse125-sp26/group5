#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>


namespace SoLoud {
class Soloud;
class Wav;
}

namespace shared {
struct SoundEmitter;
}

class AudioEngine {
public:
    bool init();
    void shutdown();
    void update();

    // One-shot 3D sound from SoundEventPacket
    void playSound(uint32_t soundId, float x, float y, float z,
                   float volume = 1.0f);

    // Called each frame for entities with SoundEmitter
    void updateEmitter(uint32_t entityId,
                   const shared::SoundEmitter& emitter,
                   float x, float y, float z,
                   float listenerX, float listenerY, float listenerZ);
    void stopAllForEntity(uint32_t entityId);

    void setListenerPosition(float x, float y, float z,
                             float forwardX, float forwardY, float forwardZ);

private:
    SoLoud::Soloud* soloud_ = nullptr;
    std::unordered_map<uint32_t, SoLoud::Wav*> sounds_;

    // entityId → (soundId → SoLoud handle)
    std::unordered_map<uint32_t,
        std::unordered_map<uint32_t, unsigned int>> activeHandles_;

    void loadSound(uint32_t soundId, const std::string& path);
    void startLayer(uint32_t entityId, uint32_t soundId,
                    float x, float y, float z, float volume);
    void stopLayer(uint32_t entityId, uint32_t soundId);
    bool isLayerActive(uint32_t entityId, uint32_t soundId) const;
};