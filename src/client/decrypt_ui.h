#pragma once

struct ClientGame;
struct Graphics;

namespace decrypt_ui {

void ensureAssetsLoaded(Graphics& graphics);
void drawDecryptOverlay(Graphics& graphics, ClientGame& game);
void onDecryptActivated();

}  // namespace decrypt_ui
