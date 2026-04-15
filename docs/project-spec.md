---
layout: page
title: Project Spec
permalink: /project-spec/
---

## Project Description
### About this game: 
**Recollection** is a cozy game about retrieving fading memory. You play as employees of the memory recovery service, where you go into the client's dreams per their request to retrieve their fading memory, painting their favorite moments back to colors by collaboratively solving little puzzles before the timer strikes and they wake up. Beware! You will have to work together as a team. The demo map features and elderly client, who came to simply retrieve and recollect their good memories in their life. :) 

### Landscape 
The initial version features a map rendered in black and white. The map is divided into 4 regions signifying the client's elderhood, adulthood, teenage and college years, and infancy. Each region will randomly spawns one memory fragment within its bounds, which is tied to a small puzzle that requires input from all 4 players to solve, though how they choose to collaborate is up to them. After the puzzle is solved, the map will be painted with beautiful color, signifying the recollection of current area being complete.   
**All players must be in the same region and progress through subsequent maps together, in a "reversed" linear order, in hope of capturing the feeling of "traveling back in time".**
| Time of Life | Season | Main Assets | Puzzle |
|---|---|---|---|
| Elderly (80 yrs) | Winter | Winter Trees, windter shrubs | Maze |
| Adulthood and middle age (40 - 80 yrs) | Autumn|  CSE Bear, |Typing puzzle |
| Teenage years and college (20 - 40 yrs) | Summer | Bone fire, Summer lush Trees, | Decription |
| Infancy and childhood (0 - 20 yrs) | Spring | Fallen Star, Flowers, | Tangram, Last main puzzle|

### Character Design 
Our models will draw some inspiration from *Overcooked* and *Webfishing*. We plan to have different character models that players can choose from when they play. The character models will be simple, having only a somewhat pill-shaped body, a head, and floating balls for hands for ease of modeling, rigging, and texturing as well as style.

- Must-Have Designs
  - Corgi
  - Cat
  - Goose
  - Gurf
- Would-Be-Nice Designs
  - Dog: Siberian, Golden Retriever
  - Cat: Black, Orange
  - Mouse
  

### Mini Puzzles 
Every memory fragment is retrieved when their associated puzzle is solved. Here is our list of puzzles.
1. Maze: The memory service customer manifests in their dream in a maze. Each of the four players control one of four cardinal directions. The team must work together to quickly navigate the maze as the customer to retrieve the memory.
2. Cube: Each player can only see one of four walls of a cube. Three walls have a hint to solve the puzzle on the fourth wall. This can be some permutation of numbers, shapes, colors, or words. The team must work together to figure out the hints and submit the correct permutation.
3. Type: Each of four players have `n` words to type. All of them must type their words quickly and accurately to solve the puzzle.
4. Memory: A randomly generated pattern of cards is shown to all the players for some set amount of time to remember. The cards will have a color, shape, and number on every card. The players will then have to recreate the pattern they were given. They are scored based on accuracy, and can proceed upon reaching a threshold. Otherwise, they'll have to retry the puzzle.
5. Decrypt: The team is given an encrypted phrase. They \[ are given / must find out \] the key, then successfully decrypt the phrase.

--- 
### 1. What kind of game are you planning to build?

Our game is a 3D co-op experience where a team of four or more players take on the role of employees in a memory recovery service. Players enter a client’s fading dreamscape, a gray, decaying world, and work together to restore it by retrieving scattered memory fragments. As players explore the regions, they collaborate to recover these fragments, gradually bringing color and life back to each area. The team must communicate and coordinate effectively to navigate the dream before time runs out and the client wakes up.

### 2. What are the goals of the game, how do players win, how do they lose?
Our game is timed, meaning the players must finish solving all puzzles, else it is considered as a fail. A percentage bar will be given to show how much was completed at the end of the game. 

#### **Win:** 
The team successfully recolors the entire client’s dream by retrieving memory fragments and restoring key areas before the client wakes up. Success is measured by overall performance rather than a single condition.

#### **Lose:** 
The team fails if they are unable to recover enough of the client’s memories before the dream ends.

#### **Replayability:** 
Each run encourages players to replay dreams to improve coordination, and recover more fragments; The location of fragments will also differ each time, so they will have to find them. This creates both a “beat your previous performance” loop and a dynamic experience driven by randomized memory fragment generation.

### 3. What are the interesting or unique aspects to your game?
- **A fading world brought back to life:** The world starts gray and colorless. As players collect memory fragments and complete area tasks, color is restored, making progress feel visually rewarding and emotionally meaningful.
- **Memory fragments as loot:** Rather than generic collectibles, players are piecing together fragments of lost memory, which adds narrative intrigue and emotional investment.
- **Speed vs. reward tension:** Players constantly choose between staying  longer in a section to collect more fragments (higher score) or pushing forward faster to beat the clock (better time). This creates meaningful moment-to-moment decision making.
- **Required cooperation:** Completing the puzzles require multiple players to work together simultaneously, making communication and teamwork essential rather than optional.
- **Replayable loop:** Memory fragments spawn randomly each run, threats vary, and players are incentivized to improve their time and score, giving the game strong replay value.

### 4. What are the list of features of your game? Prioritize them into at least three categories: "Must Have", "Would Be Really Nice", and "Cool But Only If Ahead Of Schedule".
- **Must Have**
  - 4-player co-op with synchronized movement through connected sections
  - Memory fragment collection system with random spawns
  - Color restoration mechanic — world transitions from gray to full 
    color as areas are completed
  - Time limit with a visible countdown/timer
  - Co-op task/door mechanics that require multiple players to proceed
  - Win/lose condition based on reaching the final area before time expires
  - End-of-run score based on fragments collected and time taken
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

Tech meeting: Friday, 2 P.M.

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
- Server-side tick tests (setup a tick, make sure certain actions occur)
- Server-client communications tests
- User tests
- Game playthroughs

### How will you do documentation (both internal group documentation as well as external player documentation)?
- Main documentation: [CSE 125 Group 5 Master Document](https://docs.google.com/document/d/119IUXJaZzLB1Wnq-WjzROSS_ZlbwY8B1QAaQj5ue13c/edit?usp=sharing)   
- Shared Google Drive folder to manage documents for each role
- README.MD
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
| 1 | Brainstorm + learn Blender basics | Display a window | Exchange packets between a client/server | Think about physics | Think about game logic |
| 2 | Concept art for character and map models, continue learning Blender | Render a cube that's backed by server-synchronized ECS | Propagate more attributes necessary for rendering + come up with the final architecture | Determine what is needed in terms of physics, decide on NIH syndrome or library, maybe some basic implementation | Plan out concept and pseudocode for game logic |
| 3 | Concept art for character and map models, continue learning Blender | Import arbitrary models | Implement final ECS synchronization architecture | Movement and Collision | Loot spawning |
| 4 | Basic modeling: landscape and character | Textures and materials | Networking optimizations + Level loading | Terrain | Loot collection |
| 5 | Basic modeling: landscape and character | Animations | Any additional networking features that don't go through ECS | More precise bounding boxes + game logic enablement | Moving between levels/maps |
| 6 | Refinement on models | Lighting | Data use shrinkage/proper delta states | game logic enablement | Scoring, color restoration |
| 7 | Refinement on models | GUI? | Lobby system? | game logic enablement | Time limit+timer+level specific mechanics |
| 8 | Refinement on models + model integration with code | Making everything fast and beautiful + bug fixes | cross-platform testing | game logic enablement | Level specific mechanics |
| 9 | Model integration with code + bugfixes and polish | bugfixes and polish | bugfixes and polish | bugfixes and polish | bugfixes and polish |
| 10 | Model integration with code + Demo prep + testing | Demo prep + testing | Demo prep + testing | Demo prep + testing | Demo prep + testing |

The schedule for most technical things is probably fake past week 4 since from there we dont know how much time or effort things will require, and work prioritization will more liekly be driven by the needs of people as they implement things.
