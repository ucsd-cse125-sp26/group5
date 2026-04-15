---
layout: page
title: Weekly Reports
permalink: /weekly-reports/
---
<!-- # [Weekly individual report template](https://docs.google.com/document/d/119IUXJaZzLB1Wnq-WjzROSS_ZlbwY8B1QAaQj5ue13c/edit?tab=t.1udl1fbsowz) -->
--- 
# Week 2 Group report 
Based on established idea of game mechanics, the team dug further into solidifying our game's story and structure. We came up with a better project spec. 
## Admin
Updated game name, mechanics, solidified story and background. 

## Modeling
**Rebecca**  
- For landscape: Gathered assets and planned textures, drafted 3D map layout
- Updated project spec to be more concrete and clear
- Kept team aligned via Discord announcements and weekly check-ins   

<img src="{{ '/assets/week2/season_map.png' | relative_url }}" alt="text">
<img src="{{ '/assets/week2/elevation_map.png' | relative_url }}" alt="text">
<img src="{{ '/assets/week2/asset_map.png' | relative_url }}" alt="text">

<!-- ![text](assets/week2/season_map.png)
![text](assets/week2/elevation_map.png)
![text](assets/week2/asset_map.png) -->

**Sarah** 
- Contributed to finishing puzzle design
- For character model: Completed a tutorial more advanced than current game scope to build skill
    - base model sketch
    - gurf sketch   
<img src="{{ '/assets/week2/gurf_draft.png' | relative_url }}" alt="text">
<!-- ![text](assets/week2/gurf_draft.png) -->

## Technical   
**Jacob**   
- Got a cube rendering — model rendering pipeline underway
- Delegated tasks to two teammates with clear scope   
<!-- ![text](assets/week2/week2_jacob1.gif)
![text](assets/week2/week2_jacob2.gif) -->
<img src="{{ '/assets/week2/week2_jacob1.gif' | relative_url }}" alt="text">
<img src="{{ '/assets/week2/week2_jacob2.gif' | relative_url }}" alt="text">


**Tim**   
- Pull request merged; network decoupled — client-to-server transmission working
- Infrastructure in place; needs key bindings, input callbacks, and backend tweaks
- Next: write additional callback functions (e.g. WASD movement generalized from hardcoded)  

**Alain**
- Set up Jolt physics engine in a separate branch and experimenting with it
- Working on supporting classes (!)   

**Leon & Philip** 
- Story: Finished main storyline with linear structure
- Helped settle type of puzzles centered around 4 phases of life, scaling from easy to hard


---
## Week 1 Group report 

### What we did

The team brainstormed core mechanics and narrowed down to three game concepts, with a focus on keeping scope manageable given the 10-week timeline + Initial project setup on github. 

**Code & Repo setup** 
- Repo/build/tooling scaffolded: cross-platform build scripts (build.sh, build-windows.sh, build-linux-gcc.sh), CMake project (CMakeLists.txt), and clangd support via compile_commands.json + build_lsp.sh.
- Dependencies + dev environment established: external libs are tracked under lib/ (GLFW, GLAD, ENet, EnTT, Dear ImGui), with a Nix devshell available (flake.nix, flake.lock).
- Project planning foundations drafted: initial project spec + team roles/process documented (spec.md, plus links to the main doc/brainstorm board in README.MD).

**General Design Principles Agreed On**
- Timed, co-op game for a team of 4
- Single map (no stages) to keep development feasible
- Score-based replayability; day/night modes for difficulty variation
- Key inspirations: Overcooked, Among Us, Keep Talking and Nobody Explodes

**Idea 1 — Item Scramble (Arcade Style):** Players collect randomly spawning items across a city map within a time limit. Features speed boosts, upgrades, and enemy entities. No win/lose condition — just beat your high score. Pros: easy to develop, infinitely replayable. Cons: may get boring without stakes.

**Idea 2 — Road Run (Sectioned Map):** Players progress through a linear map with distinct rooms, collecting loot and collaborating to unlock doors via communication puzzles. Has a clear win/lose condition tied to the timer, making stakes feel real. Emphasis on player communication as the core fun factor.

**Idea 3 — Memory Realm (Narrative Co-op):** The most story-driven pitch. A gray, forgotten world restored across 3 maps (Courtyard → Town Street → Memory Summit). Each player has a unique ability, requiring asymmetric communication and synchronization. Restoring areas visually transforms them. Richest concept but likely the highest scope.

## Plan for next week
The team has voted on the general framework of the game. For the following week, our plan is to: 
- Solidify the format of the game by combining everyone's ideas. 
- Modeling team: Start creating concept art 
- Physnet: Work on the established simple, barebone client-server model. 

- **What went well**
N/A
- **What blocked us**
N/A
