#pragma once

struct Graphics;
struct ClientGame;

// Draws the demo debug control panel (large grouped buttons). Gameplay buttons
// push a shared::DebugCommandPacket onto game.debugQueue (the network thread
// sends it to the server); client-only graphics buttons call Graphics methods
// directly. `open` is mutated by the in-panel close box, matching the settings
// menu's pattern. No-op when `open` is false.
void drawDebugPanel(Graphics& g, ClientGame& game, bool& open);
