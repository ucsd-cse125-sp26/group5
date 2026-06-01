#pragma once

struct ClientGame;

// Draws any active puzzle HUD overlays for the current frame. Must be called
// between ImGui::NewFrame() and ImGui::Render().
void drawPuzzleHUDs(const ClientGame& game);