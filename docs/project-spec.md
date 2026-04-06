---
layout: page
title: Project Spec
permalink: /project-spec/
---

## Project Description

### Game Description

Our game is a 3D co-op collection game. Up to 4 players can team up to collect as many required items as they can within the given time limit.

### Goal

TBD.

### Win/Lose Conditions

TBD.

### Interest Points

TBD.

### Features

- **Must Have**
  - TBD.
- **Would Be Really Nice**
  - TBD.
- **Cool But Only If Ahead Of Schedule**
  - TBD.

## Group Management

### Major Roles

- Jacob Root: Tech Lead, Graphics
- Shengrui (Leon) Chen: Game Logic
- Ziyue (Tim) Liu: Networking
- Alain Zhang: Physics
- Rebecca Chen: Art Direction, Web Dev, Modeling
- Sarah Balatbat: Art Direction, Modeling
- Phillip Mai: Game Logic

### Decision-Making

Decisions will be made by consensus. In the event where there are ties, the lead of the most relevant subteam will be the main decision maker (e.g., networking architecture decisions defer to the networking lead).

### Communications & Collaboration

- We will use **Discord** as the center of communication.
- The server will host channels for brainstorming, important links/websites, art references, and subchannels for different aspects of the project.

### Schedule Management

Outside of lectures, we will have weekly full-team meetings every Tuesday at 2 P.M. We will roughly follow an Agile/Scrum-like framework:

- Each team member shares progress since the last meeting.
- The team discusses challenges and requests help/opinions as needed.
- The team agrees on next steps and assigns new tasks.
- Each subteam will also dedicate additional meeting/communication time/meeting outside of the main meeting for their respective topics.

### Weekly Group Status Reports

We will create one full group report based on:

- Meeting notes from the weekly briefing.
- Individual reflections using the weekly report template.

One designated accumulator will summarize our reflections into the combined report.

### Weekly Group Meetings

Tuesday, 2 P.M.

Tech meeting: Friday, 2 P.M.

## Project Development

### Role Designations

- Shengrui (Leon) Chen: Game Logic
- Jacob Root: Graphics
- Ziyue (Tim) Liu: Networking
- Alain Zhang: Physics
- Rebecca Chen: Modeling, Art
- Sarah Balatbat: Modeling, Art
- Phillip Mai: Game Logic

### Tools

- Communication: Discord
- Main documentation: CSE 125 main document
- Builds: CMake + Ninja
- Graphics: OpenGL with GLFW and (maybe) GLM
- Programming language: C++
- Testing: Github actions + INSERT TEST LIBRARY HERE
- Formatting/Linting: clang-format/clang-tidy
- Networking: ENet

### Testing

- Github Actions CI to prevent build breaks
- Unit tests
- Asserts within the code
- Server-side tick tests (setup a tick, make sure certain actions occur)
- Server-client communications tests
- User tests
- Game playthroughs

### Documentation

- Shared Google Drive folder to manage documents for each role
- README.md
- Documentation in code (e.g., file headers, function headers)
- Doxygen-generated docs from C++ comments
- Markdown files for architectural design

## Project Schedule

### High-Level Milestones

Week 2: server/client synchronized graphics

Week 4: engine feature sufficient; it should be possible to do most gameplay stuff with the engine in this state

Week 7: gameplay complete; it should be possible to "play the game" now

Week 9: feature-complete and hopefully we can do a feature freeze here to focus on reliability

TBD. (Define what each milestone means, what “done” looks like, and target dates.)

### Low-Level Milestones

| Week | Art/Sound | Graphics | Game Engine/Networking | Physics | Game Logic |
|---|---|---|---|---|---|
| 1 | TBD | Display a window | Exchange packets between a client/server | Think about physics | Think about game logic |
| 2 | Figure out sound library | Render a cube that's backed by server-synchronized ECS | Propagate more attributes necessary for rendering + come up with the final architecture | Determine what is needed in terms of physics, decide on NIH syndrome or library, maybe some basic implementation | Plan out concept and pseudocode for game logic |
| 3 | Play sounds | Import arbitrary models | Implement final ECS synchronization architecture | Movement and Collision | Loot spawning |
| 4 | Play sounds backed by ECS | Textures and materials | Networking optimizations + Level loading | Terrain | Loot collection |
| 5 | TBD | Animations | Any additional networking features that don't go through ECS | More precise bounding boxes + game logic enablement | Moving between levels/maps |
| 6 | TBD | Lighting | Data use shrinkage/proper delta states | game logic enablement | Scoring, color restoration |
| 7 | TBD | GUI? | Lobby system? | game logic enablement | Time limit+timer+level specific mechanics |
| 8 | Add final sounds | Making everything fast and beautiful + bug fixes | cross-platform testing | game logic enablement | Level specific mechanics |
| 9 | bugfixes and polish | bugfixes and polish | bugfixes and polish | bugfixes and polish | bugfixes and polish |
| 10 | Demo prep + testing | Demo prep + testing | Demo prep + testing | Demo prep + testing | Demo prep + testing |

The schedule for most technical things is probably fake past week 4 since from there we dont know how much time or effort things will require, and work prioritization will more liekly be driven by the needs of people as they implement things.
