---
layout: page
title: Final Project
permalink: /final-project/
---

Put your final deliverables here (or link to them):

- Final build/download link
- Final report
- Trailer/demo video
- Postmortem / lessons learned

---

## A. Project Review

### Game Concept: How and why did your game concept change from initial concept to what you implemented?

Our initial concept was a story-driven 4-player co-op game inspired by atmospheric titles like *Journey*, where a gray, forgotten world is slowly brought back to color as players recover memory fragments together. We wanted a positive tone—not a dark tragedy, but something about helping someone remember the good parts of their life (like an elderly person rediscovering college days) and leaving with a hopeful feeling.

The core idea stayed the same in the final build. We still have a colorless dreamscape, four players working together, memory fragments, and color returning section by section. What changed was mostly framing: we settled on **Recollection**, where the team plays as employees of a memory recovery service entering a client's dream, rather than a lone silent journey. We also narrowed scope from multiple separate maps down to one overworld with four seasonal regions. A few puzzle types from early brainstorming (typing, cube hints, card memory, decrypt) were replaced with the four minigames we actually shipped. These changes were about keeping the demo explainable and finishable in ten weeks, not about changing what the game feels like to play.

---

### Design: How does your final project design compare to the initial design, and what are the reasons for the differences, if any?

In terms of core gameplay, the final design is close to what we planned. We still have 4-player co-op, a single map divided into four life-stage regions (Winter → Fall → Summer → Spring), cooperative puzzles, fragment collection, and color restoration as the main reward.

The differences are mostly cuts for simplicity. We originally listed five puzzle types; the final game has four seasonal minigames (maze, fall challenge, summer escape, tangram). Some puzzles were dropped because they did not fit the shared minigame framework we ended up building—overworld triggers, server state transitions, and optional 2D overlays—rather than because we changed our minds about co-op. We also simplified progression: instead of fully random fragment hunting and generic door puzzles, we used section barriers, fragment pickup, and expanding color zones so players always had a clear sense of what to do next. A strict timer and detailed scoring were planned early on; the demo focuses more on completing all four sections and reaching credits. Character models were scaled back to placeholders and a smaller set of animal avatars so we could spend more effort on the landscape and seasonal environments. The "travel back through a life" structure and the gray-to-color mechanic are still what we hoped for.

---

### Schedule: How does your final schedule compare with your projected schedule, and what are the reasons for the differences, if any?

Our schedule mostly followed the big milestones we set: basic client/server multiplayer, a playable engine, a first playable slice, then a full demo. Week 2 had synchronized rendering; by Week 4 the engine could support real gameplay work; by Week 9 we were in polish mode rather than adding new systems.

The main slowdowns were earlier than the original week-by-week table assumed. We spent a lot of time in the first few weeks deciding exactly what game we were making and how the core mechanisms should work—when a player can enter the next section, how fragment pickup unlocks color, how minigames start and end, and what the last step of a run is before credits. Those rules had to be agreed on by the whole team and implemented as shared server-side patterns (state machine, section gating, packet contracts) before individual puzzles could be built on top without conflicting. That design work pushed "full gameplay complete" from the Week 7 target into Weeks 7–9, even though we still had a working build at the end of most weeks.

We also lost time on client and engine infrastructure—map loading, rendering pipeline, minigame embedding, and getting four clients to stay in sync—not on abandoning the plan. Once the progression flow was locked, remaining work was mostly filling in seasonal puzzles inside that framework. Overall we stayed on track enough to ship a complete demo; the slip was in how long the underlying game-flow design took, not in missing the final milestone.

---

## B. General Questions

1. Describe your development environment (tools, build workflow, multi-platform support, tips for future groups).
2. What group mechanics decisions worked out well, and which ones did not? Why?
3. Which aspects of the implementation were more difficult than you expected, and which were easier? Why?
4. Which aspects of the project are you particularly proud of? Why?
5. What was the most difficult software problem you faced, and how did you overcome it?
6. Detail your toolchain for modeling, exporting, and loading meshes, textures, and animations.
7. For networking, physics, audio, or GUI libraries—which did you use and would you use them again?
8. If you used an LLM as part of your development process, what did you use it for? How well did it work?
9. If you used a language other than C++, describe the environments and tools you used.
10. How many lines of code did you write? (State how you counted.)
11. What lessons about group dynamics did you learn?
12. Looking back, what would you do differently and what would you do again?
13. Which courses at UCSD best prepared you for CSE 125?
14. What were the most valuable things you learned in the class?

---

### 2. What group mechanics decisions worked out well, and which ones did not? Why?

Splitting the team by area—art, graphics, networking, game logic, physics—worked well overall. Everyone had a clear lane and we could parallelize once the engine was standing.

What we would do differently is the order of work at the beginning. Our team is small enough that game logic really depends on a working graphics and networking base first—you cannot build puzzles or progression until you can see entities move and sync across clients. Early on, it might have been more efficient if more people helped get a simple scene online (basic render + basic multiplayer) before splitting networking/physics from pure game logic. Right now game logic sometimes waited on engine pieces that were still in progress. A short "everyone on the foundation" phase, then a cleaner split into graphics vs. game logic, would probably have made the first playable slice come together faster.

---

### 3. Which aspects of the implementation were more difficult than you expected, and which were easier? Why?

**Harder than expected: graphics and cross-platform work.** Graphics took more time than we planned—not just "make it look good," but building debug tools, lighting, the render pipeline, and infrastructure that other systems depend on. Sometimes the graphics layer became the bottleneck for game mechanics: a puzzle idea is easy to describe on paper, but fitting it into the actual render path (overlays, cameras, state transitions) is where the difficulty showed up. Getting details right at the end was harder than getting a rough version working at the start, especially when using AI-assisted coding—the generated code often needs careful manual adjustment to match what we actually wanted. We also hit a lot of platform issues: something would work on macOS but break on Windows or Linux, and tracking those down ate time we did not budget for.

**Easier than expected: core game logic at a high level.** The basic ideas—fragments, section gating, cooperative puzzles, state transitions—were straightforward to design once we agreed on the rules. Writing the first version of a minigame was usually not the hard part; polishing it and wiring it through graphics, networking, and physics was.

---

### 4. Which aspects of the project are you particularly proud of? Why?

We are most proud of how graphics, art, music, and story fit together into one coherent feel. The gray-to-color world, seasonal environments, skyboxes, and audio all push the same idea: recovering warm memories from a faded dream. That atmosphere does a lot of the work explaining what the game is about without heavy dialogue. When those pieces connect—visual mood, sound, and the gentle plot framing—it feels like a real game rather than a tech demo with puzzles bolted on.

---

### 7. For networking, physics, audio, or GUI libraries—which did you use and would you use them again?

We built in **C++** with **CMake + Ninja** and used the following libraries:

- **ENet** for client/server networking (UDP-based, reliable and unreliable channels for input and state sync). Lightweight and enough for a 4-player game; we would use it again.
- **EnTT** for ECS on both client and server (entities, components, registries for game state). Clean API and good fit for a multiplayer sim; would use again.
- **Jolt Physics** for server-side physics (player movement, puzzle objects, collision). Good performance and easier to work with than rolling our own; would use again.
- **GLFW + GLAD + OpenGL** for the client window and rendering; **GLM** for math.
- **Dear ImGui** for debug UI, settings menus, and in-game overlays.
- **Assimp** for loading `.glb` / model assets; **stb_image** for textures.
- **SoLoud** for audio playback on the client.
- **Google Test** for unit tests, run through **GitHub Actions** CI on Linux, macOS, and Windows.

Overall these were solid choices. ENet and EnTT stayed out of our way once the packet and ECS patterns were set. Jolt required some setup but handled real physics puzzles well. The main pain was not the libraries themselves but integrating them across platforms—getting the same build to pass on Mac, Linux, and Windows with CI.

---

### 13. Which courses at UCSD best prepared you for CSE 125?

**CSE 120** (Operating Systems), **CSE 123** (Networking), and **CSE 167** (Computer Graphics) were the most helpful. Even though we used AI tools heavily during development, those classes gave the high-level understanding we needed to know what we were building—how client/server systems talk, why the server should own game state, and how a render pipeline is structured. When something broke (sync issues, platform differences, graphics bugs), that background made it easier to reason about the problem instead of guessing.

---

### 14. What were the most valuable things you learned in the class?

The most valuable lesson was how a team actually ships a large project: clear roles, shared documentation, and a lot of communication about direction—not just about code. A big chunk of the work was making sure everyone agreed on what we were building and how systems connect (specs, architecture notes, weekly alignment), so people could work independently without stepping on each other. Planning and formalizing decisions early turned out to be as important as writing code—maybe more. Good docs and a reasonable shared plan let the team maximize output; without that, even fast individual work does not add up to a playable game.
