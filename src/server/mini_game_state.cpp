#include "mini_game_state.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "server_game.h"
#include "server_network.h"
#include "shared/components.h"
#include "shared/net/packet_utils.h"

namespace {

constexpr float kBallSize = 0.12f;
constexpr float kBallSpeed = 0.65f;

void broadcastSpawn(ServerGame& game,
                    const std::vector<entt::entity>& entities) {
  if (!game.network || entities.empty()) return;
  auto buf =
      serializeEntities(game.registry, game.componentRegistry,
                        shared::PacketType::SPAWN_ENTITY, entities, false);
  net::broadcastRaw(game.network->getHost(), buf.data(), buf.size());
}

void broadcastDespawn(ServerGame& game, entt::entity entity) {
  if (!game.network || entity == entt::null || !game.registry.valid(entity) ||
      !game.registry.all_of<shared::Entity>(entity)) {
    return;
  }

  shared::DespawnPacket pkt;
  pkt.type = shared::PacketType::DESPAWN_ENTITY;
  pkt.entityId = game.registry.get<shared::Entity>(entity).id;
  net::broadcastPacket(game.network->getHost(), pkt);
}

class BallDemoMiniGame : public IMiniGameState {
 public:
  explicit BallDemoMiniGame(uint32_t sessionId) : sessionId_(sessionId) {}

  void onEnter(ServerGame& game) override {
    auto [sessionEntityId, sessionEntity] = new_entity(game);
    (void)sessionEntityId;
    game.registry.emplace<shared::MiniGameSession>(
        sessionEntity, sessionId_, uint32_t{0}, shared::MiniGameType::BALL_DEMO,
        shared::MiniGamePhase::RUNNING, uint32_t{0}, uint32_t{0}, 1.0f, 1.0f);

    auto [ballEntityId, ballEntity] = new_entity(game);
    (void)ballEntityId;
    game.registry.emplace<shared::MiniGame2D>(ballEntity, sessionId_,
                                              uint8_t{1});
    game.registry.emplace<shared::Renderable2D>(
        ballEntity, shared::Renderable2DType::SPRITE, 0.5f - kBallSize * 0.5f,
        0.5f - kBallSize * 0.5f, kBallSize, kBallSize, 1.0f, 1.0f, 1.0f, 1.0f,
        std::string("brick"));

    sessionEntity_ = sessionEntity;
    ballEntity_ = ballEntity;

    broadcastSpawn(game, getEntities(game));
  }

  void onExit(ServerGame& game) override {
    broadcastDespawn(game, ballEntity_);
    broadcastDespawn(game, sessionEntity_);

    if (ballEntity_ != entt::null && game.registry.valid(ballEntity_)) {
      game.registry.destroy(ballEntity_);
    }
    if (sessionEntity_ != entt::null && game.registry.valid(sessionEntity_)) {
      game.registry.destroy(sessionEntity_);
    }

    sessionEntity_ = entt::null;
    ballEntity_ = entt::null;
  }

  void update(ServerGame& game, float dt) override {
    if (!game.registry.valid(ballEntity_) ||
        !game.registry.all_of<shared::Renderable2D>(ballEntity_)) {
      return;
    }

    float axisX = 0.0f;
    float axisY = 0.0f;
    auto inputView = game.registry.view<shared::PlayerInput>();
    for (auto ent : inputView) {
      const auto& pi = inputView.get<shared::PlayerInput>(ent);
      if (pi.keys & KEY_MINIGAME_RIGHT) axisX += 1.0f;
      if (pi.keys & KEY_MINIGAME_LEFT) axisX -= 1.0f;
      if (pi.keys & KEY_MINIGAME_UP) axisY += 1.0f;
      if (pi.keys & KEY_MINIGAME_DOWN) axisY -= 1.0f;
    }
    axisX = std::clamp(axisX, -1.0f, 1.0f);
    axisY = std::clamp(axisY, -1.0f, 1.0f);
    if (axisX != 0.0f && axisY != 0.0f) {
      constexpr float kInvSqrt2 = 0.70710678f;
      axisX *= kInvSqrt2;
      axisY *= kInvSqrt2;
    }

    auto& ball = game.registry.get<shared::Renderable2D>(ballEntity_);
    ball.x =
        std::clamp(ball.x + axisX * kBallSpeed * dt, 0.0f, 1.0f - ball.width);
    ball.y =
        std::clamp(ball.y + axisY * kBallSpeed * dt, 0.0f, 1.0f - ball.height);

    if (game.registry.valid(sessionEntity_) &&
        game.registry.all_of<shared::MiniGameSession>(sessionEntity_)) {
      auto& session =
          game.registry.get<shared::MiniGameSession>(sessionEntity_);
      session.elapsedMs += static_cast<uint32_t>(std::lround(dt * 1000.0f));
    }
  }

  void removePlayer(uint32_t /*playerEntityId*/) override {}

  shared::MiniGameType type() const override {
    return shared::MiniGameType::BALL_DEMO;
  }

  uint32_t sessionId() const override { return sessionId_; }

  std::vector<entt::entity> getEntities(ServerGame& game) const override {
    std::vector<entt::entity> entities;
    if (sessionEntity_ != entt::null && game.registry.valid(sessionEntity_)) {
      entities.push_back(sessionEntity_);
    }
    if (ballEntity_ != entt::null && game.registry.valid(ballEntity_)) {
      entities.push_back(ballEntity_);
    }
    return entities;
  }

 private:
  uint32_t sessionId_ = 0;
  entt::entity sessionEntity_ = entt::null;
  entt::entity ballEntity_ = entt::null;
};

// clang-format off
constexpr uint8_t kMazeCols = 10;
constexpr uint8_t kMazeRows = 10;
constexpr uint8_t W = 1; // wall
constexpr uint8_t F = 0; // floor
constexpr uint8_t kMazeLayout[kMazeRows][kMazeCols] = {
  {W,W,W,W,W,W,W,W,W,W},
  {W,F,F,F,W,F,F,F,F,W},
  {W,F,W,F,W,F,W,W,F,W},
  {W,F,W,F,F,F,F,W,F,W},
  {W,F,W,W,W,W,F,W,F,W},
  {W,F,F,F,F,W,F,F,F,W},
  {W,W,W,F,W,W,W,W,F,W},
  {W,F,F,F,F,F,F,F,F,W},
  {W,F,W,W,W,W,W,W,F,W},
  {W,W,W,W,W,W,W,W,W,W},
};
// clang-format on

constexpr int kMazeStartCol = 1;
constexpr int kMazeStartRow = 1;
constexpr int kMazeGoalCol = 8;
constexpr int kMazeGoalRow = 7;

class MazeMiniGame : public IMiniGameState {
 public:
  explicit MazeMiniGame(uint32_t sessionId) : sessionId_(sessionId) {}

  void onEnter(ServerGame& game) override {
    const auto logW = static_cast<float>(kMazeCols);
    const auto logH = static_cast<float>(kMazeRows);

    auto [sessionEntityId, sessionEntity] = new_entity(game);
    (void)sessionEntityId;
    game.registry.emplace<shared::MiniGameSession>(
        sessionEntity, sessionId_, uint32_t{0}, shared::MiniGameType::MAZE,
        shared::MiniGamePhase::RUNNING, uint32_t{0}, uint32_t{0}, logW, logH);
    sessionEntity_ = sessionEntity;

    auto [tmId, tmEntity] = new_entity(game);
    (void)tmId;
    game.registry.emplace<shared::MiniGame2D>(tmEntity, sessionId_, uint8_t{0});
    game.registry.emplace<shared::Renderable2D>(
        tmEntity, shared::Renderable2DType::TILEMAP, 0.0f, 0.0f, logW, logH,
        1.0f, 1.0f, 1.0f, 1.0f, std::string{});

    shared::TilemapRenderable2D tm;
    tm.cols = kMazeCols;
    tm.rows = kMazeRows;
    tm.tiles.resize(kMazeCols * kMazeRows);
    for (int r = 0; r < kMazeRows; ++r)
      for (int c = 0; c < kMazeCols; ++c)
        tm.tiles[r * kMazeCols + c] = kMazeLayout[r][c];
    game.registry.emplace<shared::TilemapRenderable2D>(tmEntity, tm);
    tilemapEntity_ = tmEntity;

    auto [goalId, goalEntity] = new_entity(game);
    (void)goalId;
    game.registry.emplace<shared::MiniGame2D>(goalEntity, sessionId_,
                                              uint8_t{1});
    game.registry.emplace<shared::Renderable2D>(
        goalEntity, shared::Renderable2DType::SPRITE,
        static_cast<float>(kMazeGoalCol), static_cast<float>(kMazeGoalRow),
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, std::string("flag"));
    goalEntity_ = goalEntity;

    auto [playerId, playerEntity] = new_entity(game);
    (void)playerId;
    game.registry.emplace<shared::MiniGame2D>(playerEntity, sessionId_,
                                              uint8_t{2});
    game.registry.emplace<shared::Renderable2D>(
        playerEntity, shared::Renderable2DType::SPRITE,
        static_cast<float>(kMazeStartCol), static_cast<float>(kMazeStartRow),
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, std::string("player"));
    playerEntity_ = playerEntity;

    broadcastSpawn(game, getEntities(game));
  }

  void onExit(ServerGame& game) override {
    for (auto ent :
         {playerEntity_, goalEntity_, tilemapEntity_, sessionEntity_}) {
      broadcastDespawn(game, ent);
      if (ent != entt::null && game.registry.valid(ent))
        game.registry.destroy(ent);
    }
    sessionEntity_ = entt::null;
    tilemapEntity_ = entt::null;
    goalEntity_ = entt::null;
    playerEntity_ = entt::null;
  }

  void update(ServerGame& /*game*/, float /*dt*/) override {}

  void removePlayer(uint32_t /*playerEntityId*/) override {}

  shared::MiniGameType type() const override {
    return shared::MiniGameType::MAZE;
  }

  uint32_t sessionId() const override { return sessionId_; }

  std::vector<entt::entity> getEntities(ServerGame& game) const override {
    std::vector<entt::entity> entities;
    for (auto ent :
         {sessionEntity_, tilemapEntity_, goalEntity_, playerEntity_}) {
      if (ent != entt::null && game.registry.valid(ent))
        entities.push_back(ent);
    }
    return entities;
  }

 private:
  uint32_t sessionId_ = 0;
  entt::entity sessionEntity_ = entt::null;
  entt::entity tilemapEntity_ = entt::null;
  entt::entity goalEntity_ = entt::null;
  entt::entity playerEntity_ = entt::null;
};

}  // namespace

void MiniGameStateManager::requestStart(ServerGame& game) {
  if (active_) return;
  start(game, std::make_unique<BallDemoMiniGame>(nextSessionId_++));
}

void MiniGameStateManager::requestStop(ServerGame& game) {
  if (!active_) return;
  active_->onExit(game);
  active_.reset();
}

void MiniGameStateManager::start(ServerGame& game,
                                 std::unique_ptr<IMiniGameState> state) {
  if (active_ || !state) return;
  active_ = std::move(state);
  active_->onEnter(game);
}

void MiniGameStateManager::update(ServerGame& game, float dt) {
  auto inputView = game.registry.view<shared::PlayerInput>();
  for (auto ent : inputView) {
    const auto& pi = inputView.get<shared::PlayerInput>(ent);
    if (pi.keys_newly_pressed & KEY_START_2D_MINIGAME) {
      requestStart(game);
    }
    if (pi.keys_newly_pressed & KEY_STOP_2D_MINIGAME) {
      requestStop(game);
      return;
    }
  }
  if (active_) active_->update(game, dt);
}

void MiniGameStateManager::removePlayer(uint32_t playerEntityId) {
  if (active_) active_->removePlayer(playerEntityId);
}

std::vector<entt::entity> MiniGameStateManager::getActiveEntities(
    ServerGame& game) const {
  if (!active_) return {};
  return active_->getEntities(game);
}

uint32_t MiniGameStateManager::activeSessionId() const {
  return active_ ? active_->sessionId() : 0;
}
