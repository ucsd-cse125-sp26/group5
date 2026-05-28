---
layout: page
title: Jacob Root — Individual Report
permalink: /project-spec/jacob-root-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

## Weekly Notes

### Week 8 (May 28)

🚨🚨🚨I updated the weekly report AGAIN, since you're reading this today morning you should go check out the group weekly also. It's under my name there. 🚨🚨🚨

1. What were your concrete goals for the week?

- merge rendering megapr
- skybox stuff
- helping integrate puzzles and stuff
- windows misc testing, windows perf testing
- demo box testing

1. What goals were you able to accomplish?

- merge rendering megapr
- skybox stuff
- helping integrate puzzles and stuff

1. If the week went differently than you had planned, what were the reasons?

I was not able to connect usiong nomachine

1. What are your specific goals for the next week?

- finish game

1. What did you learn this week, if anything (and did you expect to learn it)?

idk

1. What is your individual morale (which might be different from the overall group morale)?
CSE 123's PA 2a was unforutnately not good, I had a double digit number of clarification questions

### Week 7 (May 21)

🚨🚨🚨I updated the weekly report AGAIN, since you're reading this today morning you should go check out the group weekly also. It's under my name there. 🚨🚨🚨

1. What were your concrete goals for the week?

- merge anims
- head motion for characters
- integrate grayscale with phillip pr
- finish toon shading
- fix distance shadows
- loading screen/start menu
- windows misc testing, windows perf testing
- perf improvements
- skyboxes for rebecca
- map object season tags (maybe)
- map object instancing (maybe)
- shadow alpha
- debug menu
- code cleanup

1. What goals were you able to accomplish?

- head motion for characters
- integrate grayscale with phillip pr
- finish toon shading
- fix distance shadows
- loading screen/start menu
- shadow alpha
- graphics settings

1. If the week went differently than you had planned, what were the reasons?

I got around as much as I expected to get done done. Unfortunately this led gui to get delayed another week.

1. What are your specific goals for the next week?

- merge rendering megapr
- skybox stuff
- windows misc testing, windows perf testing
- demo box testing
- helping integrate puzzles and stuff

1. What did you learn this week, if anything (and did you expect to learn it)?

cel/toon shading

1. What is your individual morale (which might be different from the overall group morale)?
idk, I realized I don't evaluate morale I just make something up or use this slot to air grievances about CSE 123. I currently have no new substantive grievances regarding the class.

### Week 6 (May 14)

🚨🚨🚨I updated the weekly report with some new images, since you're reading this today morning you should go check out the group weekly also. It's under my name there. 🚨🚨🚨

1. What were your concrete goals for the week?

- skeletal animations
- vram profiling
- 2d rendering if needed
- debug UI
- input refactor + debug console
- instancing (maybe)
- actually run the game on windows
- delegate subset of below
- loading screen (maybe)

1. What goals were you able to accomplish?

- skeletal animations (sidebranch)
- vram profiling (pr)
- 2d rendering if needed (-->tim)
- actually run the game on windows
- merged maploading and the big rendering pr
- site update gha
- rendering alpha fix
- toon shading + configurable grayscale (sidebranch)
- figured out why white spotches appeared on map
- exporter improvements (pr)

1. If the week went differently than you had planned, what were the reasons?

I got around as much as I expected to get done done. Unfortunately this led gui to get delayed another week.

1. What are your specific goals for the next week?

- merge anims
- head motion for characters
- integrate grayscale with phillip pr
- finish toon shading
- fix distance shadows
- loading screen/start menu
- windows misc testing, windows perf testing
- perf improvements
- skyboxes for rebecca
- map object season tags (maybe)
- map object instancing (maybe)
- shadow alpha
- debug menu
- code cleanup

1. What did you learn this week, if anything (and did you expect to learn it)?

blender gltf exporter internals 💀, more blender stuff, relearned skanims, basic thinking about toon shading, that geometry shaders are slow for some reason

1. What is your individual morale (which might be different from the overall group morale)?
idk, I realized I don't evaluate morale I just make something up or use this slot to air grievances about CSE 123. The latest PA is ambiguous is a number of ways. I was hoping things would improve with the trend between 1a and 1b but I guess I was overly optimistic. I wonder how broken the autograder is right now. I actually was asked about ARP in an interview my 2nd year and forgot about it, maybe that's why I got catfished into doing full stack at that company (I thought I would be doing networking).

### Week 5 (May 07)

1. What were your concrete goals for the week?

- shadows, hdr, bloom, deferred shading, framebuffer debugging
- skeletal animations
- make it possible to collaborate on map (invent worse p4 from first principles)
- Figure out a solution for ECS metadata in map editing
- vram profiling
- multiple maps (maybe)
- delegate subset of below
- debug UI
- input refactor + debug console
- loading screen (maybe)
- sound

1. What goals were you able to accomplish?

- shadows, hdr, bloom, deferred shading, framebuffer debugging
- make it possible to collaborate on map (invent worse p4 from first principles)
- Figure out a solution for ECS metadata in map editing
- multiple maps
- delegate sound

1. If the week went differently than you had planned, what were the reasons?

Midterms were unfortunate, so less got done than was ideal. Largely this manifested as things being completed but not being fully polished and merged.

1. What are your specific goals for the next week?

- skeletal animations
- vram profiling
- 2d rendering if needed
- debug UI
- input refactor + debug console
- instancing (maybe)
- actually run the game on windows
- delegate subset of below
- loading screen (maybe)

1. What did you learn this week, if anything (and did you expect to learn it)?

This week was largely applying stuff I knew already.

1. What is your individual morale (which might be different from the overall group morale)?
High. Workload was high but now I should have more time.

### Week 4 (Apr 30)

1. What were your concrete goals for the week?

- design work
- ecs resources
- input refactor
- debug console
- proper maps (maybe - hook up with debug console somehow)
- shadows
- HDR/bloom
- deferred shading (maybe - potential prereq for HDR/shadows as it involves thinking about quads and fb textures)
- fbdebug (maybe)
- physics debug mode
- basic frametime profiling/warnings (maybe)
- skeletal animation (maybe - head movement on player would be nice but fine with pushing 1 week)
- work with tim on smoothness
- work with alain on integrating physics
- work with leon and phillip on minigame rendering
- planning work for next technical steps
- loading screen (maybe)

1. What goals were you able to accomplish?

- map creation in blender + coupled map loading
- blinn-phong shading
- sent ecs design resources
- physics cleanup

1. If the week went differently than you had planned, what were the reasons?

Changed focus onto the map editor because this would help unblock the actual game working. Did some extra work on rendering stuff (shadows, hdr, deferrred) but didn't finish that in time due to PA business + unexpected social commitments. Did not get as much done as I would've liked, but goals are aspirational and I think the current stack is manageable. I think the way I split my goals last time was not ideal so I started grouping them by what I'd probably do at a time.

1. What are your specific goals for the next week?

- shadows, hdr, bloom, deferred shading, framebuffer debugging
- skeletal animations
- make it possible to collaborate on map (invent worse p4 from first principles)
- Figure out a solution for ECS metadata in map editing
- vram profiling
- multiple maps (maybe)
- delegate subset of below
- debug UI
- input refactor + debug console
- loading screen (maybe)
- sound

1. What did you learn this week, if anything (and did you expect to learn it)?

gltf internals, blender gltf export details, assimp gltf handling. I didn't expect to learn this, and this direction was unexpected, but probably better than what I initially envisioned for map editing.

it turns out assimp supports importing lights, unfortunately the docs didn't say this anywhere

1. What is your individual morale (which might be different from the overall group morale)?
high; I have a large stack but it seems pretty manageable. Next week has midterms so I am theoretically busy.

### Week 3 (Apr 23)

1. What were your concrete goals for the week?

- physics debug mode graphics support
- give people resources on ECS
- non-player entities
- debug tooling
- basic testing
- farm out subset of above to others
- better lighting (ecs-synced)
- gha
- design work for full game tech
- graphics refactor

1. What goals were you able to accomplish?

- non-player entity (technically, not as much as I wanted to do)
- skybox/cubemap (ecs-controlled)
- ecs-controlled lighting
- github actions
- some design work
- graphics refactor

1. If the week went differently than you had planned, what were the reasons?

physics wasn't merged so I couldn't do debug tooking for it yet. Stuff came up so I decided to work on some graphics improvements instead of more time consuming things that required more care like proper maps, the full design, etc. Graphics hard to test so I'll do it later. broad strokes of design were decided (primarily use ECS) since that plays to our strengths and reduces complexity, might change later we'll see though, and more thinking is needed on how 2d minigames get integrated. testing for graphics moved down in priorities because it is hard and doesn't bring much benefit. didn't have time for debug console due to research deadline. I set goals aspirationally, so not completing everything is expected.

1. What are your specific goals for the next week?

- design work
- ecs resources
- input refactor
- debug console
- proper maps (maybe - hook up with debug console somehow)
- shadows
- HDR/bloom
- deferred shading (maybe - potential prereq for HDR/shadows as it involves thinking about quads and fb textures)
- fbdebug (maybe)
- physics debug mode
- basic frametime profiling/warnings (maybe)
- skeletal animation (maybe - head movement on player would be nice but fine with pushing 1 week)
- work with tim on smoothness
- work with alain on integrating physics
- work with leon and phillip on minigame rendering
- planning work for next technical steps
- loading screen (maybe)

1. What did you learn this week, if anything (and did you expect to learn it)?

learned about graphics techniques: deferred rendering, shadows (point and directional), shadow smoothness, skybox, ssao, HDR, bloom, blending, various perf optimization techniques. expected to learn eventually but not this week

thought about performance, ECS architecture stuff, glfw text input handling

1. What is your individual morale (which might be different from the overall group morale)?
6.7/10, largely due to busy week academically and an anticipated busy week next week.

### Week 2 (Apr 16)

1. What were your concrete goals for the week?

- render cube
- render bear
- basic materials and lighting
- github actions
- ecs synchronize models position and camera

1. What goals were you able to accomplish?

- render cube
- render bear
- basic materials and lighting
- antialiasing
- ecs synchronize models position and camera

1. If the week went differently than you had planned, what were the reasons?
Unexpected time sinks due to debate tournament and cse 123. Decided sleep > github actions for a few days + I didn't want to treesmash someone's in progress PR.
2. What are your specific goals for the next week?

- physics debug mode graphics support
- give people resources on ECS
- non-player entities
- debug tooling
- basic testing
- farm out subset of above to others
- better lighting (ecs-synced)
- gha
- design work for full game tech
- graphics refactor

1. What did you learn this week, if anything (and did you expect to learn it)?
learned opengl, how to do lighting and asset importation and stuff, skeletal animation basics, outlines of some directions to improve render quality

2. What is your individual morale (which might be different from the overall group morale)?
individual morale was impacted by the cse 123 PA quality. it is surprising that such pa quality is permissible in a top 20 cs school. given networking as a field is so entertwined with the ietf it is very surprising that whoever wrote the pa must have never even seen a copy of rfc 2119. in contrast to 123, it is refreshing to be working on a team composed of competent people.
