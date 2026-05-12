#include "server_memory_system.h"
#include "server_game.h"
#include "shared/components.h"

void colorizeSection(ServerGame& game, Shared::SectionSeasonMap season) {
    // Colorize only entities that belong to the selected season.
    // Uses season-specific tag components (WinterTag/FallTag/SummerTag/SpringTag)

		//need to check boundaries for each season then add season tags for all entities within each boundary

    using namespace shared;

    // Color any entity that has a SeasonComponent whose season matches
    // the requested season.
    bool ok = true;
    switch (season) {
        case SectionSeasonMap::WINTER: ok = RestoreWinterColor; break;
        case SectionSeasonMap::FALL: ok = RestoreFallColor; break;
        case SectionSeasonMap::SUMMER: ok = RestoreSummerColor; break;
        case SectionSeasonMap::SPRING: ok = RestoreSpringColor; break;
    }
    if (!ok) return;

    auto view = game.registry.view<RenderInfo, OverworldTag, SeasonComponent>();
    for (auto entity : view) {
        auto& sc = view.get<SeasonComponent>(entity);
        if (sc.season == season) {
            auto& render = view.get<RenderInfo>(entity);
            render.isColorized = true;
        }
    }
}