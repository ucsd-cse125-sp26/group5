---
layout: page
title: Sarah Balatbat — Individual Report
permalink: /project-spec/sarah-balatbat-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

## Week 4
### Goals
- [x] Create concept art for other MVPs
  - [x] Goose
  - [x] Mouse
- [ ] Create reference sheets for existing concept art
  - [ ] Gurf - WIP
- [ ] Model for existing reference sheets
  - [ ] Corgi
- [ ] Create decryption puzzle symbols
- [ ] Continue tutorial for rigging lesson

### Achieved

- Goose concept art
  - Goose design references
  <img src="{{ '/assets/week4/sarah/goose-refs.png' | relative_url }}" alt="Goose whiteboard references">
  - First draft of Goose A (mvp) and Goose B (ideal)
  <img src="{{ '/assets/week4/sarah/goose.jpg' | relative_url }}" alt="Goose design draft">
  - Goose A version 2 - bigger head like other models, bigger eyes more to the sides of head, semi-realistic beak
  <img src="{{ '/assets/week4/sarah/goose-a2.jpg' | relative_url }}" alt="Goose design A version 2 draft">
  - Goose B cleaned up - more realistic design, drew inspiration from *Untitled Goose Game*
  <img src="{{ '/assets/week4/sarah/goose-b.jpg' | relative_url }}" alt="Goose design B cleaned up">
- Mouse concept art
  - Mouse design references
  <img src="{{ '/assets/week4/sarah/mouse-refs.png' | relative_url }}" alt="Mouse whiteboard references">
  - Considering balance between realism and cuteness (real mice look cute too! hard to stylize though)
  <img src="{{ '/assets/week4/sarah/mouse.jpg' | relative_url }}" alt="Mouse design">
- Gurf concept art / ref sheet
  - Gurf design references - I made Gurf while making teaching materials in EDS 124BR and he is #MySon
  <img src="{{ '/assets/week4/sarah/gurf-refs.png' | relative_url }}" alt="Gurf whiteboard references">
  - Colored concept art - considering maw shape, Rebecca prefers the top but I'm kinda liking the bottom; might change with modeling
  <img src="{{ '/assets/week4/sarah/colored-gurf.png' | relative_url }}" alt="Gurf colored concept art">

  
### Progress Evaluation

Designed in little pockets of time I had. Got sick so I can't really be mad at myself for not getting as much as I wanted done. Not too bad progress given that and courseload.

### Upcoming Goals

- [ ] Create reference sheets for existing concept art
  - [ ] Gurf
  - [ ] Goose
  - [ ] Mouse
- [ ] Model for existing reference sheets
  - [ ] Corgi
- [ ] Create decryption puzzle symbols
- [ ] Continue tutorial for rigging lesson

### Lessons Learned

- I really just have to use whatever pockets of time I have to develop things
- Taking the time to rest lets me get back on my feet faster than if I tried to work through my sickness
- I need to remember to have fun

### Individual Morale

Being sick sucked. CSE 123 continues to be a heavy stressor, especially with midterm season being up on us. I still feel discouraged that I'm not making as much things in the time that I have as other teammates, but I think I just gotta buckle up and do it.

## Week 3
### Goals
- [ ] Continue tutorial for rigging lesson
- [x] Start modeling base model
- [ ] Create concept art for other MVPs
  - [ ] Goose
  - [x] Corgi
  - [ ] Mouse
- [ ] Create decryption puzzle symbols

### Achieved
- Corgi concept art
  - Created and iterated on design with Rebecca
    <img src="{{ '/assets/week3/sarah/sarah1.jpg' | relative_url }}" alt="Corgi model concept art">
    <img src="{{ '/assets/week3/sarah/corgi-rebecca.jpg' | relative_url }}" alt="Corgi model concept art iteration">
  - Created front and profile view references for modeling
    <img src="{{ '/assets/week3/sarah/sarah4.png' | relative_url }}" alt="Corgi model reference sheet">
- Base model
  - Created base model front and profile ref sheet
    <img src="{{ '/assets/week3/sarah/sarah3.png' | relative_url }}" alt="Base model reference sheet">
  - Started base model on Blender
    <img src="{{ '/assets/week3/sarah/sarah5.png' | relative_url }}" alt="Base model">
    <img src="{{ '/assets/week3/sarah/sarah6.png' | relative_url }}" alt="Base model front view">
    <img src="{{ '/assets/week3/sarah/sarah7.png' | relative_url }}" alt="Base model side view">
- Other character design ideas - "Would-Be-Nice" category
  - Fox
  - Moose
  
### Progress Evaluation

Progress went slower than I hoped due to other coursework (again). Also experienced some mild anxiety regarding starting modeling, but this anxiety happens with most things I know I need to start soon. 

### Upcoming Goals

- [ ] Create concept art for other MVPs
  - [ ] Goose
  - [ ] Mouse
- [ ] Create reference sheets for existing concept art
  - [ ] Gurf
- [ ] Model for existing reference sheets
  - [ ] Corgi
- [ ] Create decryption puzzle symbols
- [ ] Continue tutorial for rigging lesson

### Lessons Learned

- There are a lot of Blender shortcuts to learn, so I'm trying to keep my mind open to learning them. 
- Unexpected things can come up during development (i.e., sickness) so I need to be realistic with the progress I do in a week and be okay with that.
- Inkscape is useful for making the reference sheets with vector images, will probably adopt it into the character design pipeline.

### Individual Morale

Still holding onto the motivation of being able to play my character. Got sick so that's frustrating, can't really avoid that when it's transmitted within the household. Other courses also stressing me out (like CSE 123) and getting in the way of development. I get worried I won't be able to do enough with the time and energy that I have. I feel like I'm lagging behind compared to everyone else on the team. Hopefully changes next week.

## Week 2
### Goals
- [x] Solidify game design details
- [x] Start creating concept art
- [x] Start learning modeling on Blender

### Achieved
- Assigned design roles
  - Rebecca took up landscape / map design
  - My role is on character models
- Game design details
  - Brainstormed playable character designs
    - MVP: Gurf (cat), Corgi (dog), Goose, Mouse
    - Would-Be-Nice: cat variants (black, orange), dog variants (Siberian, golden retriever)
  - Brainstormed puzzle mini games for unlocking memory fragments
    - Memory: Players are all given a randomly generated pattern of cards for some set amount of time. The cards will have a color, shape, and number on every card. The players have to recreate the pattern they were given. They are scored based on accuracy and must meet a threshold to proceed. Otherwise, they'll retry with another pattern of cards.
    - Decryption: The team is given an encrypted phrase. They must decrypt it under some set of constraints.
      - Set A: Provide the key of a monoalphabetic substitution cipher i.e., a set of symbols for all English letters A-Z. The players can use this key to decrypt the encrypted phrase.
      - Set B: Provide a subset of the monoalphabetic substitution cipher such that there are less symbols to look at but still contains all the letters needed for decryption and some unnecessary ones.
      - Set C: Indicate that the phrase is encrypted under Caesar cipher but provide no key (involves codebreaking, likely very difficult without prior knowledge). The Caesar cipher functions such that the encrypted letters is shifted some amount from the original. For example, under an E-shift Caesar cipher, the letter A maps to E, and all the other letters maintain the expected order (B -> F, C -> G, D -> H, etc.)
      - Set D: Provide one letter as the key without disclosing it's a Caesar cipher (also involves codebreaking, likely even more difficult than Set C)
- Concept art
  - Sketched base model: Disjointed head, body, and spherical hands. Markers for ears. Roughly inspired by Webfishing, Overcooked, and Mii Plaza player models.
  <img src="{{ '/assets/week2/sarah/wk2-sarah-base-model-concept-art.jpg' | relative_url }}" alt="Base model concept art">
  - Sketched Gurf model: Same disjointed pieces as base model, but some different proportions to emphasize Gurf's chubbiness. There are also ideas of emotes as a Would-Be-Nice feature.
  <img src="{{ '/assets/week2/sarah/wk2-gurf-concept-art.jpg' | relative_url }}" alt="Gurf model concept art">
- Modeling progress
  - Followed a [Blender tutorial](https://youtu.be/FwkPW5LEGs8) for creating a low-polygon cat.
  <img src="{{ '/assets/week2/sarah/wk2-tutorial1.png' | relative_url }}" alt="Low poly cat progress on Blender (front orthographic)">
  <img src="{{ '/assets/week2/sarah/wk2-tutorial2.png' | relative_url }}" alt="Low poly cat progress on Blender (right orthographic)">


### Progress Evaluation

The progress went slower than I originally hoped, mainly because of other responsibilities and partially because of "Senioritis". Upon looking at what I've gotten done though, it's actually not that bad.

### Upcoming Goals

- Continue tutorial for rigging lesson
- Start modeling base model
- Create concept art for other MVPs
  - Goose
  - Corgi
  - Mouse
- Create decryption puzzle symbols

### Lessons Learned

- I'm getting the hang of modeling on Blender. It's not too bad once I'm in a groove, and the computer I'm working on doesn't have latency issues.
- There is a lot of compromise that comes with designing a game with a full team. I had an idea this would be the case, but it didn't hit until we started realizing we have varying ideas of what we wanted the game to look like, how we want the gameplay to proceed, etc.

### Individual Morale

I'm mainly motivated by getting to play a character I designed in a story I helped build. The gameplay has changed drastically over the first two weeks of designing. I'm not dissatisfied with the themes we came up with since I'm a big fan of themes revolving around memories, but there's parts of the gameplay (such as collecting) that I wish we got to keep more of. We still have the collecting aspect but it's not really to the degree I originally envisioned. I do like how the puzzles are shaping up, as well as the story.

I'm somewhat excited to see what we'll end up with. I prefer to err towards less and simpler things to implement because I worry we'll go too far out of scope, but the tech team having things running and the fun we might be able to have gives me some hope.

## Week 1
### What we did

Brainstormed core mechanics with the team and narrowed down to three game concepts. Maintained focus on keeping scope manageable given the 10-week timeline. Initial project setup on github. 

**Code & Repo setup** 
- Helped draft project spec: set up document outline based on project spec questions.

**General Design Principles Agreed On**
- Timed, co-op game for a team of 4
- Single map (no stages) to keep development feasible
- Score-based replayability; day/night modes for difficulty variation
- Key inspirations: Overcooked, Among Us, Keep Talking and Nobody Explodes

**Idea 1 — Item Scramble (Arcade Style):** Players collect randomly spawning items across a city map within a time limit. Features speed boosts, upgrades, and enemy entities. No win/lose condition — just beat your high score. Pros: easy to develop, infinitely replayable. Cons: may get boring without stakes.

**Idea 2 — Road Run (Sectioned Map):** Players progress through a linear map with distinct rooms, collecting loot and collaborating to unlock doors via communication puzzles. Has a clear win/lose condition tied to the timer, making stakes feel real. Emphasis on player communication as the core fun factor.

**Idea 3 — Memory Realm (Narrative Co-op):** The most story-driven pitch. A gray, forgotten world restored across 3 maps (Courtyard → Town Street → Memory Summit). Each player has a unique ability, requiring asymmetric communication and synchronization. Restoring areas visually transforms them. Richest concept but likely the highest scope.

## Plan for next week
- Solidify game design details
- Start creating concept art
- Start learning modeling on Blender

- **What went well**
  - Lots of ideas for brainstorming
- **What blocked us**
  - Consensus voting





