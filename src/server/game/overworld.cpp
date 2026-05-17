#include "server/game/overworld.h"

#include "server/server_game.h"
#include "shared/components.h"
#include "shared/input.h"
#include "server/server_memory_system.h"

namespace {

bool winterSectionCompleted(const ServerGame& game) {
  auto view = game.registry.view<shared::SectionController>();
  for (auto e : view) {
    const auto& sc = view.get<shared::SectionController>(e);
    if (sc.type == shared::SectionSeasonMap::WINTER && sc.completed)
      return true;
  }
  return false;
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
  if (winterStillActive) {
    auto gameSectionView = game.registry.view<shared::GameSection>();
    for (auto entity : gameSectionView) {
      auto& gameSection = gameSectionView.get<shared::GameSection>(entity);
      gameSection.currentActiveSeason = shared::SectionSeasonMap::WINTER;
    }
  }
}

void ProcessFragmentPickups(ServerGame& game) {
  constexpr float PICKUP_RADIUS_SQR = 4.0f * 4.0f;

  auto fragmentView = game.registry.view<shared::FragmentComponent, shared::Position>();
  auto playerView = game.registry.view<shared::PlayerInput, shared::Position>();

  for (auto fragEntity : fragmentView) {
    auto& fragment = fragmentView.get<shared::FragmentComponent>(fragEntity);
    if (fragment.isPickedUp) continue;

    const auto& fragPos = fragmentView.get<shared::Position>(fragEntity);

    for (auto playerEntity : playerView) {
      const auto& playerPos = playerView.get<shared::Position>(playerEntity);
      const auto& playerInput = playerView.get<shared::PlayerInput>(playerEntity);
      
      float dx = fragPos.x - playerPos.x;
      float dy = fragPos.y - playerPos.y;
      float dz = fragPos.z - playerPos.z;
      float distSqr = (dx * dx) + (dy * dy) + (dz * dz);

      if (distSqr <= PICKUP_RADIUS_SQR && (playerInput.keys_newly_pressed & KEY_INTERACT)) {
        fragment.isPickedUp = true;
        game.registry.remove<shared::RenderInfo>(fragEntity);

        auto sectionView = game.registry.view<shared::SectionController>();
        for (auto secEntity : sectionView) {
          auto& section = sectionView.get<shared::SectionController>(secEntity);
          if (section.type == fragment.season) {
            section.completed = true;
            break;
          }
        }

        colorizeSection(game, fragment.season);
        break;
      }
    }
  }
}

void tickOverworldGameLogic(ServerGame& game, float dt) {
  MoveInMainMap(game, dt);
  ProcessFragmentPickups(game);
}

bool RestoreWinterColor(const ServerGame& game) {
  return winterSectionCompleted(game);
}

void GatherAtExitSwitch(ServerGame& game, entt::entity switchEnt, float minX,
                        float minY, float maxX, float maxY,
                        unsigned requiredPlayersInZone) {
  if (switchEnt == entt::null ||
      !game.registry.all_of<shared::SwitchComponent>(switchEnt))
    return;
  if (!winterSectionCompleted(game)) return;

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
                     entt::entity switchEnt, entt::entity fallSectionEnt) {
  if (!winterSectionCompleted(game)) return;
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

  if (fallSectionEnt != entt::null &&
      game.registry.all_of<shared::SectionController>(fallSectionEnt)) {
    game.registry.get<shared::SectionController>(fallSectionEnt).unlocked =
        true;
  }

  for (auto e : game.registry.view<shared::GameSection>()) {
    game.registry.get<shared::GameSection>(e).currentActiveSeason =
        shared::SectionSeasonMap::FALL;
  }
}
