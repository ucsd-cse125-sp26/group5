#include "server_memory_system.h"
#include "server_game.h"
#include "shared/components.h"

void colorizeSection(ServerGame& game) {
    //add in cases for each of the seasons boolean
    //figure out which section fragment is picked up, and color in that section
    if (RestoreWinterColor) {
        auto view = game.registry.view<shared::RenderInfo, shared::OverworldTag>();
        
        for (auto entity : view) {
            auto& render = view.get<shared::RenderInfo>(entity);
            render.isColorized = true; 
        }
    }
}