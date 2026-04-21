---
layout: page
title: Game1 — What This Game Is
permalink: /game-logic/game1/
---

[Back to Game Logic Notes]({{ '/game-logic/' | relative_url }}) · [MVP and code map]({{ '/game-logic/game1/mvp/' | relative_url }})

---

## What this game is (compact)

Four players work for a **Memory Recovery Service**. They enter one client’s **fading dream**: a **single connected map** split into **four life chapters** played in reverse life order (**Winter → Autumn → Summer → Spring**). The world starts **gray**; recovering each chapter brings **color** back.

The client is an **elder** revisiting their life for **nostalgia**—not a fantasy dungeon crawl. The run is **timed**; finish the arc before the dream ends.

**Core fantasy:** teamwork restores memory; **progression is linear** in reverse order (finish Winter, then Autumn, then Summer, then Spring).

**Scope (important):** Gameplay is **2D**—movement and **all cooperative puzzles** live on a **plane** (e.g. top-down or side-view) using **x / y** only. There is **no 3D puzzle logic** in v1; art can still *look* layered, but simulation stays 2D.

Full narrative: [Story and Full Plot]({{ '/game-logic/story/' | relative_url }}).

---

## How you play

1. **Connect** up to four clients to the server; each gets a **player entity** (see MVP for current tech).
2. **Move** with WASD-style input; the **server** applies movement and **broadcasts** positions so everyone sees the same world.
3. **Per chapter (design target):** explore the **active section only** on the **2D plane**, solve a **four-player cooperative 2D puzzle** (maze, pads, patterns, etc.), then **unlock** the next section and a **2D story panel** (illustration reward).
4. **Win / lose:** complete the life arc (and/or fragment goals) **before the timer hits zero**; otherwise partial or fail state with score or completion percent.

**Co-op rule of thumb:** clients send **intent** (keys, later puzzle actions); the **server** decides outcomes (walls, puzzle state, timers).

---

## Where to read next

- **[MVP]({{ '/game-logic/game1/mvp/' | relative_url }})** — smallest shippable slice and how it maps to our repo (`protocol`, server loop, future work).
