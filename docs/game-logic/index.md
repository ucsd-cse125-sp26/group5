---
layout: page
title: Game logic overview
permalink: /game-logic/
---

## When a run starts

The game begins the moment four players are connected and ready. A single run starts, a global timer begins counting down, and the world is rendered entirely in black and white.

## Map layout and chapter order

The map is one continuous space divided into four regions, each representing a chapter of the client's life. The regions are always visited in the same order: Winter first, then Autumn, then Summer, and finally Spring. Players cannot skip ahead or choose a different order.

## Memory fragments and region puzzles

Each region has one memory fragment hidden somewhere within its bounds. The exact location of the fragment is randomized at the start of every run, so players have to explore to find it. Finding the fragment is not enough on its own. Once the fragment is located, it triggers a cooperative puzzle that all four players must participate in to solve. The puzzle is specific to that region.

- Winter uses a maze where each player controls one cardinal direction and the whole team must navigate together.
- Autumn uses a typing challenge where each player has their own set of words to type.
- Summer uses a decryption puzzle.
- Spring uses a tangram puzzle and serves as the final challenge.

## During a puzzle (cooperation rules)

While the puzzle is active, all four players must be present in the region. Players cannot begin the puzzle unless everyone is there, and the puzzle cannot be completed unless all players contribute. This is not optional cooperation, it is a hard rule enforced by the server. The server watches whether all players are in the region, whether the puzzle conditions are being met, and whether the final solution is correct.

## After a puzzle is solved

When a puzzle is solved, two things happen immediately. The memory fragment is collected and counted toward the team's score, and color is restored to that region. The world visually changes from black and white to full color in that area, and that change is permanent for the rest of the run. Progression then unlocks and all players move together to the next region. No player can advance ahead of the group.

## Server authority

The server owns everything that matters for the run. It holds the timer, tracks which regions are complete, stores where each fragment spawned, manages the state of each puzzle, records the score, and determines whether the team has won or lost. Clients send their inputs and intentions, such as where a player wants to move or what answer they are submitting, and the server decides what actually happens. The client only renders what the server says is true.

## How a run ends

The run ends in one of two ways. If the team completes all four regions before the timer runs out, they win. The final score is calculated based on how many fragments were recovered and how much time remained. If the timer hits zero before all four regions are done, the run ends in a loss. Either way, a completion percentage is shown at the end so the team knows how far they got.

## Scoring, replay, and the core loop

The scoring exists to encourage replayability. Because fragment locations are randomized each run, the team will have a different experience every time. Players are incentivized to run it again to improve their time, find fragments faster, and coordinate more smoothly on the puzzles. The core loop is: explore, find the fragment, solve the puzzle together, restore the region, move on, repeat four times before time runs out.
