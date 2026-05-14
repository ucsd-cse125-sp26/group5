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

	SeasonColorBounds boundsForSeason(shared::SectionSeasonMap season) {
		constexpr float kMinZ = -500.0f;
		constexpr float kMaxZ = 500.0f;

		switch (season) {
			case Shared::SectionSeasonMap::WINTER:
				// Stage 1: Start + Winter Unlocked
				return {40.0f, 25.0f, kMinZ, 90.0f, 105.0f, kMaxZ};
					
			case Shared::SectionSeasonMap::FALL:
				// Stage 2: Start + Winter + Autumn Unlocked
				return {40.0f, 25.0f, kMinZ, 170.0f, 105.0f, kMaxZ};
					
			case Shared::SectionSeasonMap::SUMMER:
				// Stage 3: Start + Winter + Autumn + Summer Unlocked
				return {40.0f, -105.0f, kMinZ, 170.0f, 105.0f, kMaxZ};
					
			case Shared::SectionSeasonMap::SPRING:
				// Stage 4: The Whole Map Unlocked
				return {-170.0f, -105.0f, kMinZ, 170.0f, 105.0f, kMaxZ};
		}
		
		// Stage 0 (Default): Start Area Only
		return {40.0f, 25.0f, kMinZ, 90.0f, 55.0f, kMaxZ};
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