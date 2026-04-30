#pragma once
#include <entt/entt.hpp>
#include <memory>
static constexpr uint32_t kInvalidEntityId = UINT32_MAX;
struct ServerGame;
struct PlayerAvatars;

enum class StateType { OVERWORLD, MAZE };

// ── Game states ───────────────────────────────────────────

class IGameState {
 public:
  virtual ~IGameState() = default;
  virtual void onEnter(ServerGame& game) = 0;
  virtual void onExit(ServerGame& game) = 0;
  virtual void update(ServerGame& game, float dt) = 0;
  virtual StateType getStateType() const = 0;
  virtual entt::entity getClientAvatar(const PlayerAvatars& slots) const = 0;
  virtual std::vector<entt::entity> getStateEntities(
      ServerGame& game) const = 0;
};

class OverworldState : public IGameState {
 public:
  void onEnter(ServerGame& game) override;
  void onExit(ServerGame& game) override;
  void update(ServerGame& game, float dt) override;
  StateType getStateType() const override { return StateType::OVERWORLD; }
  entt::entity getClientAvatar(const PlayerAvatars& slots) const override;
  std::vector<entt::entity> getStateEntities(ServerGame& game) const override;

 private:
  entt::entity lightEntity_ = entt::null;
  uint32_t lightEntityId_ = 0;
};

class MazeState : public IGameState {
 public:
  void onEnter(ServerGame& game) override;
  void onExit(ServerGame& game) override;
  void update(ServerGame& game, float dt) override;
  StateType getStateType() const override { return StateType::MAZE; }
  entt::entity getClientAvatar(const PlayerAvatars& slots) const override;
  std::vector<entt::entity> getStateEntities(ServerGame& game) const override;

 private:
  entt::entity lightEntity_ = entt::null;
  uint32_t lightEntityId_ = 0;
};

// ── GameStateManager ────────────────────────────────────

class GameStateManager {
 public:
  // Immediate transition — use only when no state is currently updating
  // (e.g. initial setup).
  void changeState(ServerGame& game, std::unique_ptr<IGameState> newState);

  // Deferred transition — safe to call from within a state's update().
  // The actual swap happens after update() returns.
  void requestStateChange(std::unique_ptr<IGameState> newState);

  void update(ServerGame& game, float dt);

  IGameState* currentState() const { return currentState_.get(); }

 private:
  std::unique_ptr<IGameState> currentState_;
  std::unique_ptr<IGameState> pendingState_;
};