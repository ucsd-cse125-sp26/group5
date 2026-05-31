#include "server/game/section_puzzle.h"

#include "server/server_game.h"
#include "shared/components.h"

namespace section_puzzle {

entt::entity findSection(const ServerGame& game,
                        shared::SectionSeasonMap season) {
  auto view = game.registry.view<shared::SectionController>();
  for (auto ent : view) {
    if (view.get<shared::SectionController>(ent).type == season) return ent;
  }
  return entt::null;
}

entt::entity findPuzzleForSection(const ServerGame& game,
                                  shared::SectionSeasonMap season) {
  const entt::entity section = findSection(game, season);
  if (section == entt::null) return entt::null;
  const uint32_t puzzleId =
      game.registry.get<shared::SectionController>(section).puzzleID;
  auto view = game.registry.view<shared::Entity, shared::PuzzleComponent>();
  for (auto ent : view) {
    if (view.get<shared::Entity>(ent).id == puzzleId) return ent;
  }
  return entt::null;
}

bool isSectionUnlocked(const ServerGame& game, shared::SectionSeasonMap season) {
  const entt::entity section = findSection(game, season);
  if (section == entt::null) return false;
  return game.registry.get<shared::SectionController>(section).unlocked;
}

bool isSectionCompleted(const ServerGame& game,
                        shared::SectionSeasonMap season) {
  const entt::entity section = findSection(game, season);
  if (section == entt::null) return false;
  return game.registry.get<shared::SectionController>(section).completed;
}

bool isSectionAvailable(const ServerGame& game,
                        shared::SectionSeasonMap season) {
  return isSectionUnlocked(game, season) && !isSectionCompleted(game, season);
}

void completeSection(ServerGame& game, shared::SectionSeasonMap season) {
  const entt::entity section = findSection(game, season);
  if (section == entt::null) return;
  game.registry.get<shared::SectionController>(section).completed = true;

  auto gameView = game.registry.view<shared::GameSection>();
  for (auto ent : gameView) {
    auto& gs = gameView.get<shared::GameSection>(ent);
    if (gs.sectionsCompleted < 4) gs.sectionsCompleted++;
    if (season == shared::SectionSeasonMap::FALL) {
      gs.currentActiveSeason = shared::SectionSeasonMap::FALL;
    }
  }
  printf("[Section] %s section completed\n",
         season == shared::SectionSeasonMap::FALL ? "Fall" : "Season");
}

}  // namespace section_puzzle
