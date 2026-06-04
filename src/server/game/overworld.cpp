#include "server/game/overworld.h"

#include <cstdio>

#include "server/game/fall_challenge.h"
#include "server/game/maze.h"
#include "server/game/section_puzzle.h"
#include "server/game/summer_escape.h"
#include "server/server_game.h"
#include "server/server_memory_system.h"
#include "server/server_network.h"
#include "shared/components.h"
#include "shared/input.h"
#include "shared/lighting.h"
#include "shared/net/packet_utils.h"
#include "shared/protocol.h"

namespace {

bool seasonCompleted(const ServerGame& game, shared::SectionSeasonMap season) {
  auto view = game.registry.view<shared::SectionController>();
  for (auto e : view) {
    const auto& sc = view.get<shared::SectionController>(e);
    if (sc.type == season && sc.completed) return true;
  }
  return false;
}

// Overworld progression order: winter → fall → summer → spring. Spring is the
// last, so it maps to itself (collecting the final fragment doesn't wrap).
shared::SectionSeasonMap nextSeasonInOrder(shared::SectionSeasonMap season) {
  switch (season) {
    case shared::SectionSeasonMap::WINTER:
      return shared::SectionSeasonMap::FALL;
    case shared::SectionSeasonMap::FALL:
      return shared::SectionSeasonMap::SUMMER;
    case shared::SectionSeasonMap::SUMMER:
      return shared::SectionSeasonMap::SPRING;
    case shared::SectionSeasonMap::SPRING:
      return shared::SectionSeasonMap::SPRING;
  }
  return season;
}

}  // namespace

void MoveInMainMap(ServerGame& game, float dt) {
  // Keep hub movement behavior consistent with existing server movement logic.
  movement_system(game, dt, StateType::OVERWORLD);

  // Keep active season in winter while winter is still unlocked and incomplete.
  bool winterStillActive = false;
  auto sectionView = game.registry.view<shared::SectionController>();
  for (auto entity : sectionView) {
    const auto& section = sectionView.get<shared::SectionController>(entity);
    if (section.type == shared::SectionSeasonMap::WINTER && section.unlocked &&
        !section.completed) {
      winterStillActive = true;
      break;
    }
  }
  if (winterStillActive && !game.debugSeasonOverride) {
    section_puzzle::setActiveSeason(game, shared::SectionSeasonMap::WINTER);
  }
}

void CompleteFragmentPickup(ServerGame& game, entt::entity fragEntity) {
  if (!game.registry.valid(fragEntity) ||
      !game.registry.all_of<shared::FragmentComponent>(fragEntity)) {
    return;
  }
  auto& fragment = game.registry.get<shared::FragmentComponent>(fragEntity);
  fragment.isPickedUp = true;
  game.registry.remove<shared::RenderInfo>(fragEntity);
  // Removing RenderInfo isn't diffed by the normal update path, so the client
  // would keep drawing the fragment. Despawn it explicitly (same reason the
  // barrier removal below sends DESPAWN_ENTITY). The entity lives on
  // server-side for its FragmentComponent/pickup state.
  if (game.network != nullptr &&
      game.registry.all_of<shared::Entity>(fragEntity)) {
    shared::DespawnPacket despawn;
    despawn.type = shared::PacketType::DESPAWN_ENTITY;
    despawn.entityId = game.registry.get<shared::Entity>(fragEntity).id;
    net::broadcastPacket(game.network->getHost(), despawn);
  }

  // Delegate section completion to the appropriate puzzle collector.
  if (fragment.season == shared::SectionSeasonMap::WINTER) {
    CollectMazeFragment(game);
  } else if (fragment.season == shared::SectionSeasonMap::FALL) {
    fall_challenge::CollectFallFragment(game);
  } else if (fragment.season == shared::SectionSeasonMap::SUMMER) {
    summer_escape::CollectSummerFragment(game);
  } else if (fragment.season == shared::SectionSeasonMap::SPRING) {
    if (!section_puzzle::isSectionCompleted(game,
                                            shared::SectionSeasonMap::SPRING)) {
      section_puzzle::completeSection(game, shared::SectionSeasonMap::SPRING);
    }
  }

  bool shouldRestore = false;
  if (fragment.season == shared::SectionSeasonMap::WINTER) {
    shouldRestore = RestoreWinterColor(game);
  } else if (fragment.season == shared::SectionSeasonMap::FALL) {
    shouldRestore = RestoreFallColor(game);
  } else if (fragment.season == shared::SectionSeasonMap::SUMMER) {
    shouldRestore = RestoreSummerColor(game);
  } else if (fragment.season == shared::SectionSeasonMap::SPRING) {
    shouldRestore = RestoreSpringColor(game);
  }
  if (shouldRestore) {
    colorizeSection(game, fragment.season);
    // Drive progression off the fragment (the door/switch system that used to
    // do this is never run): advance to the next season, swap the overworld
    // scene (skybox + directional light), and unlock the next section. The
    // final season (spring) maps to itself.
    const shared::SectionSeasonMap next = nextSeasonInOrder(fragment.season);
    if (next != fragment.season) {
      const entt::entity nextSection = section_puzzle::findSection(game, next);
      if (nextSection != entt::null) {
        game.registry.get<shared::SectionController>(nextSection).unlocked =
            true;
      }
    }
    section_puzzle::setActiveSeason(game, next);
  }

  // Permanently remove barriers for this season.
  std::vector<entt::entity> barriersToRemove;
  auto barrierView =
      game.registry.view<shared::SectionBarrierTag, shared::OverworldTag>();
  for (auto barrier : barrierView) {
    if (barrierView.get<shared::SectionBarrierTag>(barrier).season ==
        fragment.season) {
      barriersToRemove.push_back(barrier);
    }
  }
  for (auto barrier : barriersToRemove) {
    if (game.network != nullptr &&
        game.registry.all_of<shared::Entity>(barrier)) {
      shared::DespawnPacket pkt;
      pkt.type = shared::PacketType::DESPAWN_ENTITY;
      pkt.entityId = game.registry.get<shared::Entity>(barrier).id;
      net::broadcastPacket(game.network->getHost(), pkt);
    }

    game.registry.destroy(
        barrier);  // on_destroy<PhysicsBody> hook cleans up the body
  }
}

void ProcessFragmentPickups(ServerGame& game) {
  constexpr float PICKUP_RADIUS_SQR = 4.0f * 4.0f;

  auto fragmentView =
      game.registry.view<shared::FragmentComponent, shared::Position>();
  auto playerView = game.registry.view<shared::PlayerInput, shared::Position>();

  for (auto fragEntity : fragmentView) {
    auto& fragment = fragmentView.get<shared::FragmentComponent>(fragEntity);
    if (fragment.isPickedUp) continue;

    if (!game.registry.all_of<shared::RenderInfo>(fragEntity)) continue;

    const auto& fragPos = fragmentView.get<shared::Position>(fragEntity);

    for (auto playerEntity : playerView) {
      const auto& playerPos = playerView.get<shared::Position>(playerEntity);
      const auto& playerInput =
          playerView.get<shared::PlayerInput>(playerEntity);

      float dx = fragPos.x - playerPos.x;
      float dy = fragPos.y - playerPos.y;
      float dz = fragPos.z - playerPos.z;
      float distSqr = (dx * dx) + (dy * dy) + (dz * dz);
      if (distSqr <= PICKUP_RADIUS_SQR &&
          (playerInput.keys_newly_pressed & KEY_PICKUP)) {
        CompleteFragmentPickup(game, fragEntity);
        break;
      }
    }
  }
}

void SyncFragmentLights(ServerGame& game) {
  auto view = game.registry.view<shared::FragmentComponent, shared::Position>();
  for (auto e : view) {
    const auto& frag = view.get<shared::FragmentComponent>(e);
    const auto& pos = view.get<shared::Position>(e);
    // "Revealed" == visible (has RenderInfo) and not yet collected.
    const bool revealed =
        !frag.isPickedUp && game.registry.all_of<shared::RenderInfo>(e);
    const bool hasLight = game.registry.all_of<shared::PointLight>(e);
    if (revealed && !hasLight) {
      constexpr auto kAtt = shared::kDefaultPointLightAttenuation;
      // Lift the light above the fragment so its own mesh doesn't occlude the
      // shadow cubemap in every direction (Z is up).
      constexpr float kFragmentLightHeight = 2.5f;
      // Color is overridden + animated client-side; these defaults are just a
      // sane fallback. castsShadow=true so the fragment is the shadow caster.
      game.registry.emplace<shared::PointLight>(
          e, pos.x, pos.y, pos.z + kFragmentLightHeight, kAtt.constant,
          kAtt.linear, kAtt.quadratic, 0.1f, 0.1f, 0.1f, 1.0f, 1.0f, 1.0f, 1.0f,
          1.0f, 1.0f, true);
    } else if (!revealed && hasLight) {
      game.registry.remove<shared::PointLight>(e);
    }
  }
}

void tickOverworldGameLogic(ServerGame& game, float dt) {
  MoveInMainMap(game, dt);
  ProcessFragmentPickups(game);
  SyncFragmentLights(game);
}

bool RestoreWinterColor(const ServerGame& game) {
  return seasonCompleted(game, shared::SectionSeasonMap::WINTER);
}

bool RestoreFallColor(const ServerGame& game) {
  return seasonCompleted(game, shared::SectionSeasonMap::FALL);
}

bool RestoreSummerColor(const ServerGame& game) {
  return seasonCompleted(game, shared::SectionSeasonMap::SUMMER);
}

bool RestoreSpringColor(const ServerGame& game) {
  return seasonCompleted(game, shared::SectionSeasonMap::SPRING);
}

void GatherAtExitSwitch(ServerGame& game, entt::entity switchEnt, float minX,
                        float minY, float maxX, float maxY,
                        unsigned requiredPlayersInZone,
                        shared::SectionSeasonMap requiredSeason) {
  if (switchEnt == entt::null ||
      !game.registry.all_of<shared::SwitchComponent>(switchEnt))
    return;
  if (!seasonCompleted(game, requiredSeason)) return;

  auto view = game.registry.view<shared::Position, shared::OverworldTag>();
  unsigned count = 0;
  for (auto e : view) {
    const auto& pos = view.get<shared::Position>(e);
    if (pos.x >= minX && pos.x <= maxX && pos.y >= minY && pos.y <= maxY)
      count++;
  }

  if (count >= requiredPlayersInZone) {
    game.registry.get<shared::SwitchComponent>(switchEnt).switchOn = true;
  }
}

void OpenSectionDoor(ServerGame& game, entt::entity doorEnt,
                     entt::entity switchEnt, entt::entity nextSectionEnt,
                     shared::SectionSeasonMap requiredSeason,
                     shared::SectionSeasonMap nextSeason) {
  if (!seasonCompleted(game, requiredSeason)) return;
  if (doorEnt == entt::null || switchEnt == entt::null) return;
  if (!game.registry.all_of<shared::SectionDoorComponent, shared::Entity>(
          doorEnt))
    return;
  if (!game.registry.all_of<shared::SwitchComponent>(switchEnt)) return;

  auto& sw = game.registry.get<shared::SwitchComponent>(switchEnt);
  if (!sw.switchOn) return;

  const uint32_t doorNumericId = game.registry.get<shared::Entity>(doorEnt).id;
  if (sw.parent != doorNumericId) return;

  auto& door = game.registry.get<shared::SectionDoorComponent>(doorEnt);
  door.state = shared::DoorState::OPEN;

  if (nextSectionEnt != entt::null &&
      game.registry.all_of<shared::SectionController>(nextSectionEnt)) {
    game.registry.get<shared::SectionController>(nextSectionEnt).unlocked =
        true;
  }

  section_puzzle::setActiveSeason(game, nextSeason);
}
