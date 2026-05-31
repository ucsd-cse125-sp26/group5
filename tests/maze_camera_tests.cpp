#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <enet/enet.h>

#include "server/game/puzzles/maze/camera.h"
#include "server/game/puzzles/maze/trigger.h"
#include "server/server_game.h"
#include "shared/components.h"
#include "shared/puzzles/maze/layout.h"
#include "shared/puzzles/maze/defaults.h"

namespace {

entt::entity makeOverworldAvatar(ServerGame& game, uint32_t entityId, float x,
                                 float y) {
  auto ent = game.registry.create();
  game.registry.emplace<shared::Entity>(ent, entityId);
  game.registry.emplace<shared::Position>(ent, x, y, 0.0f, 1.0f, 0.0f, 0.0f,
                                            0.0f);
  game.registry.emplace<shared::OverworldTag>(ent);
  return ent;
}

void addPlayerSlot(ServerGame& game, ENetPeer* peerKey, float x, float y) {
  PlayerAvatars slots;
  slots.overworld_avatar =
      makeOverworldAvatar(game, game.nextEntityId++, x, y);
  slots.maze_avatar = entt::null;
  game.active_players[peerKey] = slots;
}

}  // namespace

TEST(MazeCamera, SnapFacesPreviewCenter) {
  ServerGame game;
  addPlayerSlot(game, reinterpret_cast<ENetPeer*>(1), -2.0f, 10.0f);

  maze_camera::snapOverworldAvatarsFaceMazePreview(game);

  const auto& pos = game.registry.get<shared::Position>(
      game.active_players[reinterpret_cast<ENetPeer*>(1)].overworld_avatar);
  glm::quat rot(pos.qw, pos.qx, pos.qy, pos.qz);
  glm::vec3 forward = rot * glm::vec3(0.0f, 1.0f, 0.0f);
  forward.z = 0.0f;
  forward = glm::normalize(forward);

  const float dx = shared::maze_preview::kLookAtX - pos.x;
  const float dy = shared::maze_preview::kLookAtY - pos.y;
  glm::vec3 desired(dx, dy, 0.0f);
  desired = glm::normalize(desired);

  EXPECT_GT(glm::dot(forward, desired), 0.95f);
}

TEST(MazeCamera, AllFacingAfterSnapWithFourPlayers) {
  ServerGame game;
  const float offsets[4][2] = {{-2.0f, 14.0f}, {2.0f, 14.0f}, {-2.0f, 18.0f},
                               {2.0f, 18.0f}};

  for (int i = 0; i < 4; ++i) {
    addPlayerSlot(game, reinterpret_cast<ENetPeer*>(static_cast<uintptr_t>(i +
                                                                           1)),
                  offsets[i][0], offsets[i][1]);
  }

  maze_camera::snapOverworldAvatarsFaceMazePreview(game);
  EXPECT_TRUE(maze_camera::allOverworldAvatarsFacingMazePreview(game));
}

TEST(MazeCamera, TriggerRegionMatchesPreviewConstants) {
  const auto layout = shared::maze_layout::Config::defaults();
  EXPECT_TRUE(maze_trigger::isInsideMazeTriggerRegion(
      shared::Position{.x = layout.triggerCenterX, .y = layout.triggerCenterY},
      layout));
  EXPECT_FALSE(maze_trigger::isInsideMazeTriggerRegion(
      shared::Position{.x = layout.boardCenterX, .y = layout.boardCenterY},
      layout));
}
