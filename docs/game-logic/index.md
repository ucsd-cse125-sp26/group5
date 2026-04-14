---
layout: page
title: Game Logic Notes
permalink: /game-logic/
---

These are our team notes for gameplay logic. This is not a textbook. It is a practical guide for whoever is implementing rules, puzzle flow, win/lose, and progression.

**How to read this:** start with [Story and Full Plot]({{ '/game-logic/story/' | relative_url }}), then [Phase A]({{ '/game-logic/phase-a/' | relative_url }}), then [Phase B-F]({{ '/game-logic/phases-b-to-f/' | relative_url }}).

---

## Quick Chinese chat summary of all phases

- Phase A: Lock the verbs first (what players/world can do) before coding big systems.
- Phase B: Define state fields and decide who owns authority (usually server in multiplayer).
- Phase C: Use state machines for run flow and puzzle flow, not one giant if-chain.
- Phase D: Use a fixed simulation timestep so logic is stable across machines.
- Phase E: Client sends intent, server validates and applies authoritative state.
- Phase F: Test logic (unit/integration/playtest) before polish.

---

## Three-layer mental model

| Layer | What it handles | Example in our game |
|------|------------------|---------------------|
| Presentation | Visuals, UI, audio | Timer HUD, color restore effects, puzzle screen |
| Simulation (game logic) | Rules and state transitions | Fragment spawn, puzzle checks, scoring, region completion |
| Transport | Network delivery | ENet packets and ownership rules |

The logic layer should answer only one thing: **given current state + input, what is the next state?**

---

## Project mapping in one line

Timer, random fragments, color restoration, 4-player puzzles, and score should all be server-authoritative. Clients mainly render and send player intent.

---

## Pages

- [Story and Full Plot]({{ '/game-logic/story/' | relative_url }})
- [Phase A - Lock Verbs First]({{ '/game-logic/phase-a/' | relative_url }})
- [Phase B-F - State, Flow, Tick, Networking, Testing]({{ '/game-logic/phases-b-to-f/' | relative_url }})

Keep these pages updated as v1 rules change.
