---
layout: page
title: Jacob Root — Individual Report
permalink: /project-spec/jacob-root-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

## Weekly Notes

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
