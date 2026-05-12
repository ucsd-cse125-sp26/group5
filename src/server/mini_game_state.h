#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <memory>
#include <vector>

#include "shared/components.h"
#include "shared/protocol.h"

struct ServerGame;

class IMiniGameState {
 public:
  virtual ~IMiniGameState() = default;

  virtual void onEnter(ServerGame& game) = 0;
  virtual void onExit(ServerGame& game) = 0;
  virtual void update(ServerGame& game, float dt) = 0;
  virtual void handleInput(ServerGame& game, uint32_t playerEntityId,
                           const shared::MiniGameInputPacket& input) = 0;
  virtual void removePlayer(uint32_t playerEntityId) = 0;

  virtual shared::MiniGameType type() const = 0;
  virtual uint32_t sessionId() const = 0;
  virtual std::vector<entt::entity> getEntities(ServerGame& game) const = 0;
};

class MiniGameStateManager {
 public:
  void requestStart(ServerGame& game);
  void requestStop(ServerGame& game);
  void start(ServerGame& game, std::unique_ptr<IMiniGameState> state);
  void update(ServerGame& game, float dt);
  void handleInput(ServerGame& game, uint32_t playerEntityId,
                   const shared::MiniGameInputPacket& input);
  void removePlayer(uint32_t playerEntityId);

  std::vector<entt::entity> getActiveEntities(ServerGame& game) const;
  uint32_t activeSessionId() const;
  bool isActive() const { return active_ != nullptr; }

 private:
  uint32_t nextSessionId_ = 1;
  std::unique_ptr<IMiniGameState> active_;
};
