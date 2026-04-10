---
layout: page
title: Phase A - Lock Verbs First
permalink: /game-logic/phase-a/
---

[Back to Game Logic Notes]({{ '/game-logic/' | relative_url }})

---

## What Phase A means

Before writing large gameplay systems, list the core **verbs/actions** in the game.

If this list is vague, implementation will drift and people will code different assumptions.

The deliverable is simple: one short page everyone agrees is the v1 behavior scope.

For our game, verbs include things like *move*, *interact*, *start puzzle*, *submit puzzle input*, *complete fragment*, and *end run*.

---

## Where our code is today

We already have one verb wired end-to-end: **Move**.

Flow is already real in the repo: client sends `KEYBOARD_INPUT`, server updates movement, clients receive `UPDATE_POSITION`.

Use that as the template for new verbs: **intent packet -> server validation -> state broadcast**.

---

## Recommended v1 vertical-slice verbs

| Priority | Verb | Plain meaning | Notes |
|---------|------|---------------|------|
| Existing | **Move** | Player movement intent | Already implemented |
| 1 | **Start run / join** | Define when run starts and timer begins | Even if v1 is "4 players connected = start" |
| 2 | **Interact (generic)** | "I want to use what is in front of me" | Reuse one interaction verb first |
| 3 | **Begin puzzle** | Server switches fragment/region into puzzle mode | Only one puzzle type needed in first slice |
| 4 | **Submit puzzle input** | A single attempt in puzzle mode | Define one payload shape for first puzzle |
| 5 | **Complete / collect** | Mark fragment solved and collected | Hook to progress and color restoration |
| 6 | **End run** | Time out or completion reached | Triggers result screen and final score |

Items to defer for now: cosmetic selection, story expansion, extra maps, advanced threats (unless milestone requires them).

---

## Minimum output from team meeting

For each verb, write one line:

**who can trigger it -> what server checks -> what state changes**

That is enough to move to Phase B.

[Next: Phase B-F]({{ '/game-logic/phases-b-to-f/' | relative_url }})
