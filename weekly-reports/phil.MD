# Phillip's Weekly individual status report 


## Week 2
1. What were your concrete goals for the week?  
I mainly wanted to read over EnTT and learn more about ECS, as well as workout the specific details of the game such as mini puzzles, story line, and logical progression.
2. What goals were you able to accomplish?  
I was able to work out with Leon our ideas for the game and reconcile them with Rebecca and Sarah's ideas so that we all get on the same page regarding mini puzzles and the progression of the gameplay/storyline. I was only able to read over EnTT and ECS very briefly. 
3. If the week went differently than you had planned, what were the reasons?  
Another one of my classes (ECE 196) is significantly more busy than I had anticipated, and I was not able to spend nearly as much time on this class as I wanted.
4. What are your specific goals for the next week?  
I would like to read more about EnTT and ECS and figure out how Leon and I will begin implementing the game's logic. I am hoping that ECE 196 is more of a front-loaded class, which would allow me to devote more time to this game project once most of my other obligations are taken care of. 
5. What did you learn this week, if anything (and did you expect to learn it)?  
I did not learn too much this week, given that I was not able to read over EnTT/ECS as much as I had wanted. However, I did get the overall gist of what ECS is and how it applies to creating game entities and how they work.
6. What is your individual morale (which might be different from the overall group morale)?  
I am honestly feeling very unsure about how much I will be able to contribute in this project. My schedule outside of this class is much worse than I thought it would be, so I hope that the next few weeks go much better than the past week did in terms of outside obligations. I am really passionate about this project and I would like to see myself contribute my own fair share and feel like a better teammate to my group.


## Week 1
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



