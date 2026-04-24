---
layout: page
title: Alain Zhang — Individual Report
permalink: /project-spec/alain-zhang-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

## Weekly Notes

### Week 3

#### Goals

- [x] Add a floor body to the physics simulation (#16)
- [ ] Implement physics debug mode
- [ ] Work on collision detection
- [x] clang-tidy setup 

#### Achieved

- clang-tidy setup 
  - Created `.clang-tidy` config with checks suited for a game project; disabled noisy ones like `performance-enum-size` that generated thousands of false positives from external libs like entt
  - Updated `format.sh` to run clang-tidy on `src/` alongside clang-format, using `--header-filter` and `--exclude-header-filter` to suppress warnings from `lib/` and `build/`
  - Fixed `build_lsp.sh` to use `-G Ninja` to match `build.sh` and avoid CMake generator conflicts
  - Fixed pre-existing enet include propagation bug via `target_include_directories(enet PUBLIC ...)`
- Implemented floor via `createFloor` function 
  - Static `BoxShape` body at z=-1 using `EMotionType::Static` and `Layers::NON_MOVING` so players land on it instead of falling indefinitely
- Made progress on physics debug mode (in progress)
- Working on collision detection (in progress)

#### Progress Evaluation

The floor implementation and clang-tidy setup went smoothly. clang-tidy had more friction than expected since the dev environment needed a lot of packages installed from scratch and `zpp_bits` had an SSH auth issue that blocked submodule cloning for a bit. Debug mode is still in progress, since it depends on debug rendering support that isn't fully in place yet. Collisions are similarly still being worked through.

#### Upcoming Goals

- [ ] Complete physics debug mode
- [ ] Finish collision detection integration

#### Lessons Learned

Got more hands-on experience with Jolt's body creation API and how static bodies interact with the simulation. Debug rendering for physics turns out to be non-trivial since it requires hooking into the graphics pipeline in ways that are still being worked out. Also learned more about how CMake propagates include paths than I ever expected to, since it turns out linking a library and getting its headers are two separate things.

#### Individual Morale

[5/10] — [Very tired. Haven't been getting much sleep.]

---
### Week 2

#### Goals
- [x] Research physics engines suitable for the project
- [x] Evaluate and select a physics library
- [x] Help Set up dev environment tooling (Doxygen)
- [x] Begin understanding Jolt Physics architecture
- [x] Add Jolt to the repo (#13)
- [x] Hook up entities to Jolt (#14)
- [x] Integrate physics into the client/server/shared architecture
- [x] Update CMakeLists and build scripts

#### Achieved

- Researched and selected Jolt Physics as the physics engine
- Set up Doxygen 
- Laid groundwork for adding Jolt to the repo 
- Learned Jolt's core concepts: rigid bodies, static bodies, layer interfaces, broadphase, BodyIDs, initialization order
- Added Jolt to the repo 
- Hooked up entities to Jolt
  - Added `PhysicsBody` component to `shared/components.h` storing a `uint32_t` bodyId to avoid Jolt types leaking into shared code
  - Added full Jolt initialization to `ServerGame` — layer interfaces (`BPLayerInterfaceImpl`, `ObjectLayerPairFilterImpl`, `ObjectVsBroadPhaseLayerFilterImpl`), `TempAllocatorImpl`, `JobSystemThreadPool`, and `PhysicsSystem::Init` in correct initialization order
  - Added `createPlayerBody` in `server_game.cpp` — capsule-shaped dynamic body created when a player connects
  - Updated `onConnect` in `main.cpp` to call `createPlayerBody` and attach result to entity via `PhysicsBody`
  - Updated game loop to step Jolt each tick and sync body positions back into ECS `Position` components
  - Diagnosed and fixed segfault caused by `TempAllocatorImpl` constructing before `RegisterDefaultAllocator` was called, found via GDB stack trace
- Integrated physics across the client/server/shared folder structure
- Updated CMakeLists with `add_subdirectory`, `target_link_libraries`, and `target_include_directories`
  - Resolved merge conflict with teammate's additions (assimp, glm, stb) and unified `target_link_libraries`
  - Fixed `server_lib` CMake target missing Jolt linking and include path

#### Progress Evaluation

The week started exploratory, mostly evaluating physics engines takes more time than it looks since you have to understand the API deeply enough to know whether it'll fit the project architecture. Tooling setup had some friction around `compile_commands.json` and header filtering. The integration work went roughly as expected; the main challenge was navigating the client/server/shared separation cleanly and figuring out what physics code belongs where, since Jolt types belong only in `server/` and plain data goes in `shared/` so the client build never depends on Jolt. CMake had some rough edges when bringing in Jolt as a subdirectory, particularly around include path propagation to the `server_lib` test target. There was also a segfault from Jolt's allocator constructing before `RegisterDefaultAllocator` was called, which had to be diagnosed with GDB.

#### Upcoming Goals

- [ ] Sync physics state with ECS alongside teammates
- [ ] clang-tidy setup 
- [ ] Continue refining the physics/ECS integration

#### Lessons Learned

Learned a lot about physics engine design, rigid vs static bodies, collisions, character controllers, and how Jolt handles initialization order and BodyIDs. Deepened understanding of CMake, specifically `add_subdirectory`, PUBLIC vs PRIVATE linking, and `CMAKE_CURRENT_SOURCE_DIR`. Also got more comfortable with Doxygen and how Jolt fits into a fixed timestep game loop. Learned how to use GDB to get a stack trace, which was the only way to figure out where the segfault was coming from. Didn't expect to go this deep into physics or CMake this early, but it was necessary to get the integration right.
#### Individual Morale

[2/10] — [I dislike taking 123 at this current moment in time...]
