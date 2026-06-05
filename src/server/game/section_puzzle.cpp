#include "server/game/section_puzzle.h"

#include "server/server_game.h"
#include "shared/components.h"
#include "shared/log.h"

namespace section_puzzle {

const char* sceneNameForSeason(shared::SectionSeasonMap season) {
  switch (season) {
    case shared::SectionSeasonMap::WINTER:
      return "night";
    case shared::SectionSeasonMap::SPRING:
      return "morning";
    case shared::SectionSeasonMap::SUMMER:
      // No "afternoon" scene exists; "noon" is the closest match.
      return "noon";
    case shared::SectionSeasonMap::FALL:
      return "sunset";
  }
  return "sunny";
}

void setActiveSeason(ServerGame& game, shared::SectionSeasonMap season) {
  for (auto e : game.registry.view<shared::GameSection>()) {
    game.registry.get<shared::GameSection>(e).currentActiveSeason = season;
  }
  const char* sceneName = sceneNameForSeason(season);
  // Only update the overworld scene; MazeTag has its own Scene anchor which
  // stays whatever spawnSceneAnchor<MazeTag> set it to.
  for (auto e : game.registry.view<shared::OverworldTag, shared::Scene>()) {
    game.registry.get<shared::Scene>(e).name = sceneName;
  }
}

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

bool isSectionUnlocked(const ServerGame& game,
                       shared::SectionSeasonMap season) {
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
  }
  if (season == shared::SectionSeasonMap::FALL) {
    setActiveSeason(game, shared::SectionSeasonMap::FALL);
  }
  LOG_DEBUG("[Section] %s section completed\n",
         season == shared::SectionSeasonMap::FALL ? "Fall" : "Season");
}

}  // namespace section_puzzle
