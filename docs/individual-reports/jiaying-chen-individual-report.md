---
layout: page
title: Jiaying Chen — Individual Report
permalink: /project-spec/jiaying-chen-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

## Week 4 
1. What were your concrete goals for the week?
This week, I wanted to: 
    - Work on and finish the landscape, hopefully I can start coloring 
    - Start making assets like the fallens star, trees, etc
2. What goals were you able to accomplish?
    - Finished working on the entire landscape, but have not started coloring yet; Finished making base model for fallen star, but was failing at making the tree leaves that I thought would be super easy. well see what happens next. 
3. If the week went differently than you had planned, what were the reasons? 
    - This week went kinda according to my plan still. 
4. What are your specific goals for the next week?
    - Start working more and more on assets, work with Jacob to understand what is the file format they specifically need 
5. What did you learn this week, if anything (and did you expect to learn it)?
    - As always, things that look simple might not be simple. 
6. What is your individual morale (which might be different from the overall group morale)?
    - I think it's pretty good; not much to comment on but I think I'm making acceptable progress.


## Week 3 
1. What where your concrete goals for the week? 
- Create a basic sketch of what the game should actaully look like, with colors and assets created. 
- Finish creating the rough landscape. 
- Mayby start looking into assets

2. What goals were you able to accomplish?
I was able to finish the sketch! This sketch contains: 
    1. Color of what the map should look like ✅
    2. Vibe of the game - the atmosphere ✅
    3. Assets each part of the map ✅

However, I started doing landscape and realize it's actually more time consuming to get things to look good than I thought. So I will be spending more time on that. 

<img src="{{ '/assets/week3/rebecca/color1.png' | relative_url }}" alt="thing">
<img src="{{ '/assets/week3/rebecca/color2.png' | relative_url }}" alt="thing">
<img src="{{ '/assets/week3/rebecca/model1.png' | relative_url }}" alt="thing">
<img src="{{ '/assets/week3/rebecca/model2.png' | relative_url }}" alt="thing">
<img src="{{ '/assets/week3/rebecca/model3.png' | relative_url }}" alt="thing">
<img src="{{ '/assets/week3/rebecca/model4.png' | relative_url }}" alt="thing">
<img src="{{ '/assets/week3/rebecca/model5.png' | relative_url }}" alt="thing">

3. If the week went differently than you had planned, what were the reasons? 
Underestimation of how time consuming the landscaping task is; I also think I should redo parts of it because I just don't like how it looks currrently. Also joined a hackathon that took the life out of me this weekend. 

4. What are your specific goals for the next week?
- Finish landscaping for sure; Start coloring the entire map and creating the path. 
- Start creating the assets and actually start coloring things there. 


5. What did you learn this week, if anything (and did you expect to learn it)?
Lots of modeling things, I think I gained a better intuition of how modeling works as a whole. So this is good.

6. What is your individual morale (which might be different from the overall group morale)?
I am trying my best to keep a balance to make sure I dont burn out, so far I have been doing a good job. 

## Week 2
1. What were your concrete goals for the week?
My current goal for the week was to: 
- Keep learning modeling and gather resources I need to create the model 
- Keep all teammates in check for progress of whole tean 
- Get basic model and design of the game set up, make sure everything is falling into place 
- Communicate next steps
- Update project spec

2. What goals were you able to accomplish?
- Learning blender modeling: Successfully familarized myself with modeling, Got assets and general plannings down for how to do the texture etc. 
- Keep all teammates in check: it seems everyone's fine with keeping a general weekly reports document. This is good. I also make announcements on the **important** channel on discord. Everything is going smoothly on this part. 
- Have a draft of how the map should look like in 3D. this is good. 
- Communicated next step in today's meeting. 
- Updated project spec from more ambiguous to more clear. 

3. If the week went differently than you had planned, what were the reasons? 
- I think I was able to get what I want to get done done. I create very careful plannings of what I need to do. 
4. What are your specific goals for the next week?
**Admin:**
- Update project spec for new stories and new puzzles brainstormed (IMPORTANT)
- Determine size of model 
- Create group report from everyone 
**Modeling** 
- Assets: start modeling complex assets
    - Rigging bear model 
    - Start fallen star model 
- Landscape: Wednesday, work with friend on advancing the draft 
Prioritize the above instead of anything else 
5. What did you learn this week, if anything (and did you expect to learn it)?
- Learned a bunch of blender stuff and Im content with what I have done 
6. What is your individual morale (which might be different from the overall group morale)?
- As basically the team leader, I need to make sure communication is good. I am in good connection with my modeling team so that's great; I am not following up on the technical team but I have trust in Jacob as the tech lead, so I am not worried. 


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





