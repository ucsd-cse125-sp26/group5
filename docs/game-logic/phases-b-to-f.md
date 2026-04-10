---
layout: page
title: Phase B-F and Practical Notes
permalink: /game-logic/phases-b-to-f/
---

[Back to Game Logic Notes]({{ '/game-logic/' | relative_url }}) · [Phase A]({{ '/game-logic/phase-a/' | relative_url }})

---

## Phase B - Define state and ownership

For each feature, answer:

- Which variables represent it? (e.g. `timeRemainingMs`, `fragmentsCollected`, `puzzlePhase`)
- Which copy is authoritative? (in multiplayer, usually server)

Simple struct notes are enough. The key rule is: do not let two systems own the same truth.

---

## Phase C - Model control flow as state machines

Use clear state enums:

- Run-level: `Lobby -> InDream -> Ending`
- Puzzle-level: `Idle -> Active -> Success/Fail`

Draw this first (whiteboard/Excalidraw/ASCII). Keep code names aligned with diagram names.

---

## Phase D - Run simulation on fixed tick

Use fixed `dt` for gameplay updates instead of frame-dependent logic.

Common loop: accumulate real time, run `simulate(dt)` for each full tick, render with interpolation if needed.

---

## Phase E - Multiplayer input pipeline

1. Client sends **intent** (e.g. interact pressed)
2. Server validates (range, phase, cooldown, permissions)
3. Server updates authoritative state and broadcasts updates

This avoids desync and reduces cheating paths.

---

## Phase F - Validate before polish

- Unit tests for pure logic (score math, checks, puzzle validators)
- Integration/scripted tests for server-client flow
- Real playtests for readability, pacing, and communication clarity

---

## Mapping to our project spec

| Spec feature | Logic owner |
|-------------|-------------|
| Timed run | Server clock/run state, client display only |
| Random fragments | Server rolls spawn, clients render |
| Color restoration | Server tracks region completion, clients lerp visuals |
| 4-player puzzle mechanics | Server tracks roles/submissions per puzzle state machine |
| Win/lose + score | Server computes and sends result |

With EnTT + ENet, keep gameplay logic in systems/small modules instead of stacking everything in `main.cpp`.

---

## Common beginner mistakes to avoid

- Putting outcome rules only on client
- Missing explicit puzzle phases
- Mixing network handlers and puzzle math too tightly
- Using frame count as timer source instead of time/ticks

---

## Suggested team reading order

1. Align v1 verbs and win/lose conditions ([Phase A]({{ '/game-logic/phase-a/' | relative_url }}))
2. Draw run + one puzzle state machines
3. Define server state structs and intent-based packet shapes
4. Build one vertical slice (move + one fragment + one simple puzzle + timer + end result)
5. Expand puzzle types and map complexity
