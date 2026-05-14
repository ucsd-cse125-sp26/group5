#include "server_memory_system.h"
#include "server_game.h"
#include "shared/components.h"

namespace {

struct SeasonColorBounds {
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};

constexpr float kColorOriginX = 0.0f;
constexpr float kColorOriginY = 0.0f;
constexpr float kColorOriginZ = 0.0f;

SeasonColorBounds makeBounds(float minX, float minY, float width, float height) {
    minX *= shared::kColorBoundsScale;
    minY *= shared::kColorBoundsScale;
    width *= shared::kColorBoundsScale;
    height *= shared::kColorBoundsScale;
    return {kColorOriginX + minX,
            kColorOriginY + minY,
            kColorOriginZ,
            kColorOriginX + minX + width,
            kColorOriginY + minY + height,
            kColorOriginZ + 1.0f};
}

SeasonColorBounds boundsForSeason(Shared::SectionSeasonMap season) {
    switch (season) {
        case Shared::SectionSeasonMap::WINTER:
            return makeBounds(0.0f, 3.0f, 5.0f, 5.0f);
        case Shared::SectionSeasonMap::FALL:
            return makeBounds(5.0f, 0.0f, 8.0f, 8.0f);
        case Shared::SectionSeasonMap::SUMMER:
            return makeBounds(0.0f, 8.0f, 13.0f, 13.0f);
        case Shared::SectionSeasonMap::SPRING:
            return makeBounds(13.0f, 0.0f, 21.0f, 21.0f);
    }
    return makeBounds(0.0f, 0.0f, 5.0f, 3.0f);
}

void updateColorBoundingBoxes(ServerGame& game, Shared::SectionSeasonMap season) {
    auto bounds = boundsForSeason(season);
    auto view = game.registry.view<shared::ColorBoundingBox, shared::PlayerInput>();
    for (auto entity : view) {
        auto& box = view.get<shared::ColorBoundingBox>(entity);
        box.minX = bounds.minX;
        box.minY = bounds.minY;
        box.minZ = bounds.minZ;
        box.maxX = bounds.maxX;
        box.maxY = bounds.maxY;
        box.maxZ = bounds.maxZ;
    }
}

}  // namespace

void colorizeSection(ServerGame& game, Shared::SectionSeasonMap season) {
    using namespace shared;

    updateColorBoundingBoxes(game, season);

    bool ok = true;
    switch (season) {
        case SectionSeasonMap::WINTER: ok = RestoreWinterColor; break;
        case SectionSeasonMap::FALL: ok = RestoreFallColor; break;
        case SectionSeasonMap::SUMMER: ok = RestoreSummerColor; break;
        case SectionSeasonMap::SPRING: ok = RestoreSpringColor; break;
    }
    if (!ok) return;

    auto playerView = game.registry.view<ColorBoundingBox, PlayerInput>();
    auto renderView = game.registry.view<RenderInfo, Position, OverworldTag>();

    for (auto entity : renderView) {
        if (game.registry.all_of<PlayerInput>(entity)) continue;
        auto& position = renderView.get<Position>(entity);
        auto& render = renderView.get<RenderInfo>(entity);

        for (auto playerEntity : playerView) {
            auto& box = playerView.get<ColorBoundingBox>(playerEntity);
            if (box.contains(position.x, position.y, position.z)) {
                render.isColorized = true;
                break;
            }
        }
    }
}