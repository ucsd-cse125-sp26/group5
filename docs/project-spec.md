---
layout: page
title: Project Spec
permalink: /project-spec/
---

## Project Description
### 1. What kind of game are you planning to build?

Our game is a 3D co-op collection game where a 4 or above players team move through a fading, gray world and restore it by collecting memory fragments. Players progress through connected sections of a decaying map,  completing tasks and collecting randomly spawned memory fragments that  bring color and life back to each area. The team must balance collecting as many fragments as possible against moving forward quickly to beat the previous speedrun record. 

### 2. What are the goals of the game, how do players win, how do they lose?
There are two ideas for the game: either limited timed, or unlimited timed but based on beating previous record. 
#### **Win:** 
The team successfully reaches the final area having restored as much of the world as possible by collecting memory fragments and completing team tasks. 

The final score still needs to be thought out; It is based on either 
  - The number of fragments collected    
  - The speed of completion   
  - Or both   

#### **Lose:** 
Depending on our discussion, two possibilities: 
1. The team fails to reach the final area before time runs out.
2. The team did not beat its previous record. 

#### **Replayability:** 
After completing a run, players are scored on their progress and collection. Future runs encourage players to beat their previous time and score, creating a "beat your record" loop.


### 3. What are the interesting or unique aspects to your game?
- **A fading world brought back to life:** The world starts gray and colorless. As players collect memory fragments and complete area tasks, color is restored, making progress feel visually rewarding and emotionally meaningful.
- **Memory fragments as loot:** Rather than generic collectibles, players are piecing together fragments of lost memory, which adds narrative intrigue and emotional investment.
- **Speed vs. reward tension:** Players constantly choose between staying  longer in a section to collect more fragments (higher score) or pushing forward faster to beat the clock (better time). This creates meaningful moment-to-moment decision making.
- **Required cooperation:** Certain doors, progress points, and  area-based tasks require multiple players to work together simultaneously, making communication and teamwork essential rather than optional.
- **Replayable loop:** Memory fragments spawn randomly each run, threats vary, and players are incentivized to improve their time and score, giving the game strong replay value.

### 4. What are the list of features of your game? Prioritize them into at least three categories: "Must Have", "Would Be Really Nice", and "Cool But Only If Ahead Of Schedule".
- **Must Have**
  - 4-player co-op with synchronized movement through connected sections
  - Memory fragment collection system with random spawns
  - Color restoration mechanic — world transitions from gray to full 
    color as areas are completed
  - Time limit with a visible countdown/timer
  - Co-op task/door mechanics that require multiple players to proceed
  - Win/lose condition based on reaching the final area before time expires, OR a "Beat your record" system 
  - End-of-run score based on fragments colle cted and time taken
  - At least 1 map and 4 player models

- **Would Be Really Nice**
  - Threats or obstacles that interfere with collection and movement
  - Area-based team tasks that offer larger rewards for cooperation 
    beyond just unlocking doors
  - 3 distinct maps with unique section layouts and obstacle types
  - Visual and audio feedback when color is restored to an area

- **Cool But Only If Ahead Of Schedule**
  - Narrative context for the memory fragments (story bits, lore, etc.)
  - Unique section types with meaningfully different collection 
    or cooperation mechanics
  - Additional maps beyond the initial 3
  - Cosmetic rewards or unlockables tied to score milestones

## Group Management

### What are the major roles in your group's management?

- Rebecca Chen: Project Coordinator, Art Direction, Modeling, Webdev 
- Jacob Root: Tech Lead, Graphics

### How will decisions be made? By leader, consensus?

- Decisions will be made by consensus. 
- In the event where there are ties, the lead of the most relevant subteam will be the main decision maker (e.g., networking architecture decisions defer to the networking lead).

### How will you communicate and collaborate online?

- We will use **Discord** as the center of communication.
- The server will host channels for brainstorming, important links/websites, art references, and subchannels for different aspects of the project.

### How will you know when you're off schedule, and how will you deal with schedule slips?

Outside of lectures, we will have weekly full-team meetings every Tuesday at 2 P.M. We will roughly follow an Agile/Scrum-like framework:

- Each team member shares progress since the last meeting.
- The team discusses challenges and requests help/opinions as needed.
- The team agrees on next steps and assigns new tasks.
- Each subteam will also dedicate additional meeting/communication time/meeting outside of the main meeting for their respective topics.

### Who will produce the weekly group status reports?

We will create one full group report based on:
1. Meeting notes from the weekly briefing.
2. Each individual will use a weekly report template to reflect on their own progress. The main accumulator will summarize our reflections. 

One designated accumulator will summarize our reflections into the combined report. Week 1's group meeting has been summarized. 

### When will you have your weekly group meetings (separate from the meeting with instructors)?
Every Tuesday, 2 P.M. Google calendar invite has been sent out to group members. 

## Project Development

### What are the development roles and who will handle them?

- Rebecca Chen: Main Project Coordinator, Art Direction, Modeling, Webdev 
- Jacob Root: Tech Lead, Graphics
- Shengrui (Leon) Chen: Game Logic
- Ziyue (Tim) Liu: Networking
- Alain Zhang: Physics
- Sarah Balatbat: Art Direction, Modeling
- Phillip Mai: Game Logic

### What tools will you use?
- Communication: Discord
- Builds: CMake + Ninja   
- Graphics: OpenGL with GLFW and (maybe) GLM   
- Programming language: C++   
- Testing: Github actions + INSERT TEST LIBRARY HERE   
- Formatting/Linting: clang-format/clang-tidy   
- Networking: ENet   
- Modeling: Blender, Procreate, Substance Painter

### Testing
- Github Actions CI to prevent build breaks
- Unit tests
- Asserts within the code
- Scenario tests
- User tests
- Game playthroughs
- Unity mockup comparisons

### How will you do documentation (both internal group documentation as well as external player documentation)?
- Main documentation: [CSE 125 Group 5 Master Document](https://docs.google.com/document/d/119IUXJaZzLB1Wnq-WjzROSS_ZlbwY8B1QAaQj5ue13c/edit?usp=sharing)   
- Shared Google Drive folder to manage documents for each role
- README.MD
- Documentation in code (e.g., file headers, function headers)
- Doxygen-generated docs from C++ comments
- Markdown files for architectural design

## Project Schedule

### High-Level Milestones

TBD. (Define what each milestone means, what “done” looks like, and target dates.)

### Low-Level Milestones

TBD. (Weekly milestones.)

#### Schedule Grid (Draft Template)

| Week | Art/Sound | Graphics | Networking | Game Engine/Physics | Game Logic |
|---|---|---|---|---|---|
| TBD | TBD | TBD | TBD | TBD | TBD |
| TBD | TBD | TBD | TBD | TBD | TBD |

