---
layout: page
title: Phillip Mai — Individual Report
permalink: /project-spec/phillip-mai-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})
## Week 8
1. What were your concrete goals for the week?  
Main goals were just to get barrier logic finished up, like adding in barriers around the whole map and for each season section.
2. What goals were you able to accomplish?  
I was able to finish up the barriers. There was some misunderstanding between Rebecca and I about what we intended the barriers to be, but we were able to get it worked out and I ended up implementing what Rebecca visualized. The logic should all be completed as well, so you can unlock next sections as soon as you pick up the fragments.
3. If the week went differently than you had planned, what were the reasons?  
This week actually went good, the barrier logic code wasn't that bad so it went relatively smoothly.
4. What are your specific goals for the next week?  
I would like to get a full run through of the maze (the first puzzle) to see if everyones code integrated correctly and then work with Leon on the Tangram puzzle. 
5. What did you learn this week, if anything (and did you expect to learn it)?  
I actually learned something that seems really obvious, but I somehow never realized before, which is that the renderInfo component only covers the model that renders for the entity, and doesn't actually create any physical body for it. So if you have a model rendered, but no physical body component, players can't "collide" with the object. 
6. What is your individual morale (which might be different from the overall group morale)?  
My morale is getting pretty low since we have a lot left to do and not much time to do it. I am also being pressured in other classes, so the stress is getting pretty high. 

## Week 7
1. What were your concrete goals for the week?  
I wanted to finish up the color restoration logic and was given the subsequent task of hooking up the color restoration logic with the fragment pickup logic.
2. What goals were you able to accomplish?  
I was able to finish everything I wanted to do, actually just today I finished testing out the fragment pickup logic that I implemented. On the server side it is hooked up to the color restoration but I can't test that out yet since I don't think we have the front end client side done just yet. We will basically have 1 full puzzle end to end finished, and it should be easy to extrapolate this into the remaining puzzles since a lot of the logic is reuseable. 
3. If the week went differently than you had planned, what were the reasons?  
This week actually went decently despite the lab assignment. The lab assignment was still terrible as usual, but I was able to finish it and it didn't hinder my work in this class too much.
4. What are your specific goals for the next week?  
I would like to get the section barrier logic finished and move onto the 2nd and 3rd puzzles. 
5. What did you learn this week, if anything (and did you expect to learn it)?  
I think I just learned a little bit about how the key presses work. In general the work I've done in the past few weeks give me a lot of insight about how the whole game system works from logic to rendering models
6. What is your individual morale (which might be different from the overall group morale)?  
My morale is now impacted by the fact that it is week 8. Somehow I did not realize it was week 8 until week 8 actually came by and now I feel the weight of the rapidly approaching deadline. 

## Week 6
1. What were your concrete goals for the week?  
Originally the goal was to implement a 3d maze generator that could render out cubes on a 2d array making up a maze, but it changed to implementing the logic for restoring color in each of the sections and working out how the section doors would work. 
2. What goals were you able to accomplish?  
I am currently 80% done with the color restoration logic, and Leon actually handled the section door logic in his most recent pull request. I was able to create a maze generator but it is unknown if that will be used at all. 
3. If the week went differently than you had planned, what were the reasons?  
This week went as planned, mainly because there was not a lab assignment due for ECE 108. This week was one of the more peaceful weeks. 
4. What are your specific goals for the next week?  
I would like to finish up my color restoration logic and begin working on something else. I suspect my next task will likely be the next puzzle after the maze. 
5. What did you learn this week, if anything (and did you expect to learn it)?  
I don't think I learned too much, but I was able to brainstorm with Jacob and try out many different ways of implementing the color restoration logic. I think I developed a sense of what is good design and what would be considered poor approaches to design, what kinds of approaches lead to better performance and less work. 
6. What is your individual morale (which might be different from the overall group morale)?  
My morale is better than last week, but we do have a Lab assignment for ECE108 to be done this week so I am living in great fear of the future. 

## Week 5
1. What were your concrete goals for the week?  
I wanted to continue working with Leon to begin implementing the lower level details of the puzzles, especially since we had worked out the implementation details with the tech team during last week's Friday meeting. 
2. What goals were you able to accomplish?  
Frankly I was not able to accomplish anything. Leon and I were just able to plan out what we wanted to do on the team's Master Google Doc.
3. If the week went differently than you had planned, what were the reasons?  
This week went extremely off track due to a lab assignment that ended up taking much longer than I thought it would, and unfortunately I was not able to contribute at all this week. 
4. What are your specific goals for the next week?  
I really want to get back on track. I want to work on puzzle game logic and really figure out how it all ties together with the modeling that Rebecca and Sarah are doing, and how my work will integrate with everyone else's work.
5. What did you learn this week, if anything (and did you expect to learn it)?  
I didn't learn much this week, mainly due to lack of time working on this class.
6. What is your individual morale (which might be different from the overall group morale)?  
My morale this week was really hit by the work in ECE 108 (the lab assignment). I really wish things had gone differently so that I could have contributed more to the team and pulled my own weight. 

## Week 4
1. What were your concrete goals for the week?  
I wanted to implement ECS for the overall game state and puzzle logic, so basically adding in components and creating the entities needed to keep progress and game attributes at game start.
2. What goals were you able to accomplish?  
I was able to implement components for things such as game state, puzzle state, doors for each section, game section, etc. I was also able to implement the function that loads in the necessary entities for keeping track of game progress at game start. 
3. If the week went differently than you had planned, what were the reasons?  
This week was roughly on track in terms of the work I able to get done. But I was a little busy from other classes as well as being a caretaker for my mom who is currently bedridden from a herniated disc. It hasn't impacted my ability to contribute to the team and do my class work but it is worth mentioning. 
4. What are your specific goals for the next week?  
I would like to move onto implementing logic for the specific puzzles, starting with the maze. Since Tim is now starting to work on game logic too, I'd like to collaborate more with him so that we can be on the same page and avoid our work conflicting with each other.
5. What did you learn this week, if anything (and did you expect to learn it)?  
I don't think I learned much this week, mainly this week was about using what I learned last week to implement some real logic and put it into practice. 
6. What is your individual morale (which might be different from the overall group morale)?  
I am now starting to get busy in ECE108 so my morale is impacted from having to juggle the workload in that class on top of the time I put into this class, but I hope I'll be able to manage this coming week. 

## Week 3
1. What were your concrete goals for the week?  
I wanted to implement a base puzzle for all the puzzle games to be extended from. I didn't have many other goals besides that.
2. What goals were you able to accomplish?  
I was able to implement the puzzle state packets that handle the data to be sent across server and client for the puzzle games. I was also able to learn more about how packets and ECS work together
3. If the week went differently than you had planned, what were the reasons?  
I actually got more done than I thought I did, mainly in terms of code comprehension and figuring out how packets work with networking and how they integrate with ECS. 
4. What are your specific goals for the next week?  
I would like to learn more about ECS. Jacob informed Leon and I that we should rather be implementing our game logic only with ECS, rather than worrying about packets and networking since that has already been taken care of. So this week I want to figure out how to implement the puzzle logic using ECS without worrying about the networking aspect. 
5. What did you learn this week, if anything (and did you expect to learn it)?  
I learned a lot about packets and how they work with ECS. The packets handle sending data across server and client and ECS uses the packets to update the entity components with the new data. 
6. What is your individual morale (which might be different from the overall group morale)?  
I am feeling a little better this week compared to last week, because I was able to get through a few big hurdles with ECE 196 and I am hopeful that I will be able to contribute more time and effort into this class from now on. The things I learned this week also give me more confidence and boost my morale. 


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



