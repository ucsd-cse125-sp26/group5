---
layout: page
title: Alain Zhang — Individual Report
permalink: /project-spec/alain-zhang-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

## Weekly Notes

### Week 9

#### Goals

- [x] Complete the falling-object survival challenge end to end (activation → survival → completion)
- [x] Replace the placeholder offset trigger box with a platform-based activation check
- [x] Implement per-participant progress tracking and a multi-segment HUD
- [x] Wire challenge completion to fragment reveal and section progression
- [x] Diagnose the post-merge regression where the challenge stopped behaving correctly
- [~] Notifications / QoL overlay system (mostly in place, back-burnered behind the regression hunt)
- [ ] Spawn fences/barriers when the challenge is triggered
- [ ] Move the trigger onto Rebecca's finalized platform instead of my placeholder

#### Achieved

**Dynamic activation trigger (`maybeActivateFallChallenge` — `fall_challenge.cpp`)**

Replaced the original placeholder trigger with a platform-based activation check. The old approach used a small trigger box offset from the visible play platform, which meant a single player could wander into it but getting several bodies to crowd into it simultaneously was nearly impossible. Since the platform itself is the natural gather point, activation now fires when *every connected player* is standing on the play platform (`playCenter` ± `playHalf`, plus a `1.5` entry margin so a player on the very edge still counts). The check is player-count agnostic — it fires whenever `insideCount == connected`, so it works for any lobby size rather than a hardcoded number. A `completed` guard prevents a finished challenge from re-arming.

**Per-participant progress + completion (`fall_challenge_system` — `fall_challenge.cpp`)**

- Each enrolled player gets a permanent slot in a 4-wide `fill[]` array tracked on `FallChallengeState`, with enrollment managed through a `participantMask` that is OR'd with everyone currently in-zone and AND'd against the connected set each tick (so disconnects cleanly drop a player and never deadlock completion).
- Survival accrues progress at `fillRate * dt`; leaving the platform drains it via a time-normalized `hitPenalty / kOffPlatformDrainSeconds * dt` so the penalty is framerate-independent rather than a per-frame flat subtraction.
- The challenge resolves only when every enrolled participant reaches `fill >= 1.0`, making it a genuinely cooperative survival objective.

**Completion → fragment reveal → section progression**

On solve, the system stops spawning, despawns all remaining cubes, tears down the hazard zones, plays the completion sound, and reveals the fall fragment by emplacing `RenderInfo` on the matching `FragmentComponent` (created render-less in the level so it only appears once earned). `CollectFallFragment` then increments the completed-section count, slotting the challenge into the overall map progression flow.

**Multi-segment HUD (`drawFallChallengeHUD` — `puzzle_hud.cpp`)**

Reworked the HUD into a single 600×44 bar split into one segment per active participant, each filling independently from its `fill[]` value (clamped to `[0,1]`) with per-slot dividers and labels, under a "Survive the falling cubes!" banner. Because it reads straight from the replicated `FallChallengeState` on the render registry, every client sees the whole team's live progress without any extra networking.

**Debugging / Issues — the post-merge regression**

After the latest merges, completing the challenge left players unable to move and drifting backwards. Root-caused it to a lifecycle ordering bug between the challenge and the movement system:

- `Knockback.remaining` (the timer during which physics, not input, owns a player's horizontal velocity) is only ticked down inside `knockback_system`.
- `knockback_system` only runs while the challenge is active — `update` early-returns on `if (!isActive(game)) return;`.
- On the solve frame, `fall_challenge_system` flips `active = false`. Any player who took a cube hit within `kKnockbackDuration` (`0.4s`) of solving has their `remaining` frozen above zero forever, because the system that would have counted it back down never runs again.
- The movement system then permanently treats them as `knocked`: WASD is discarded on X/Y and the body coasts on residual knockback velocity — exactly the "can't move / drift backwards" symptom.

Fix identified: clear `Knockback` (zero `remaining` and kill residual horizontal velocity) for all participants in the solve block, mirroring the existing one-shot cube despawn that already guards against the same "frozen on completion" failure mode. Notably the cube-cleanup path was already protected this way; the knockback path was the one spot that was missed.

**Notifications / QoL overlay system (in progress)**

Continued groundwork on an in-world notification/overlay system for teaching players how to interact with the world, building on the same ImGui HUD layer the puzzle overlays use. Functional but back-burnered this week so the regression could take priority.

**Tooling**

Fixed a silently-broken disable in `.clang-tidy`: a missing comma between two entries in the folded `Checks:` block was merging them into one space-joined token, which matched nothing and quietly re-enabled `clang-analyzer-core.BitwiseShift` (a false positive originating in the vendored `pl_mpeg` decoder). Restored the comma so the intended suppressions actually apply.

#### Progress Evaluation

Most of the week split between finishing the challenge's completion flow and hunting the movement regression. The activation-trigger refactor was the cleanest win — moving from an offset box to "everyone on the platform" removed a whole class of flakiness around getting multiple bodies into a small region. The regression was the time sink: the symptom (can't move after winning) looked unrelated to knockback until I traced *which system owns the velocity* and *when it stops running*, at which point the early-return ordering made it obvious. Good reminder that completion transitions need to actively reset transient per-player state, not just stop the systems that maintain it.

#### Upcoming Goals

- [ ] Land the knockback-clear fix in the solve path and verify across a multi-player lobby
- [ ] Spawn fences/barriers on challenge trigger to physically gate the arena
- [ ] Migrate the trigger onto Rebecca's finalized platform geometry
- [ ] Finish the notifications/QoL overlay pass now that the regression is understood
- [ ] Help with sound and movement-system integration as needed

#### Lessons Learned

The big one: when a system both *owns* a piece of state (here, horizontal velocity during knockback) and is the *only* thing that resets it, any code path that disables that system mid-effect strands the state. The cube-despawn path already accounted for this; the knockback path didn't, and the inconsistency is what produced the bug. Also reinforced that "framerate-independent" penalties (`/ dt`-normalized drains) matter even for gameplay feel, not just physics. And on tooling: a folded YAML scalar will happily swallow a missing comma and turn two list entries into one nonsense glob with zero errors — worth grepping for whenever a suppression "isn't working."

#### Individual Morale

[3/10] — Writing this at 2 a.m. before a 9 a.m. final (mercifully moved to remote). The challenge works, the bug is understood, and I have not yet started studying. I believe in Jacob who believes in me.

---

### Week 8

#### Goals

- [x] Prototype a survival challenge where players dodge objects falling from the sky for a fixed duration
- [x] Implement server-side spawning of falling hazards with even arena coverage
- [x] Implement knockback when a hazard strikes a player, integrated with the movement system
- [x] Implement the survive-to-progress mechanic and a first-pass HUD
- [~] Begin the notifications/overlay system for teaching in-world interactions
- [ ] Source audio assets (deferred to the artists)

#### Achieved

**Falling-object hazard system (`falling_objects_system` — `fall_challenge.cpp`)**

Built the server-authoritative hazard spawner that rains objects ("pumpkins") onto the play arena. Spawn positions come from a lattice/spiral burst pattern (`burstPositions`, seeded `mt19937(1337)` for reproducibility) so coverage stays even across the disk instead of clustering, which keeps the dodging fair regardless of where a player stands. Spawns are deferred into a buffer and applied after the zone view is iterated, since emplacing `Position`/`OverworldTag` mid-iteration would invalidate the view over those same pools.

Each hazard carries a `FallingObject` component with an `age` timer. Cleanup runs every tick and retires an object when either `age > 4.0s` (so landed objects don't pile up on the floor forever) or its `z < -5.0` (a kill-plane catch for anything that misses the platform and falls into the void), broadcasting a `DESPAWN_ENTITY` packet and tearing down the Jolt body via the existing `on_destroy<PhysicsBody>` hook.

**Knockback system + movement integration (`knockback_system` — `fall_challenge.cpp`, `server_game.cpp`)**

Implemented the hit response: when a falling object comes within `kKnockbackHitRadius` (`2.0`) of a player, it applies a horizontal shove (`kKnockbackPush = 13.0`) plus a small vertical pop on side hits (`kKnockbackUpPush = 5.0`) and sets `Knockback.remaining = kKnockbackDuration` (`0.4s`). The physics and knockback half-extents deliberately share one constant (`kFallingObjectHalf = 1.0`) so the visual hit radius and the collision radius can't silently drift apart; render scale is kept separate (`1.1`) so the mesh can look right without changing the world-space hit size.

Integrated this with `movement_system_for_world` so that while `Knockback.remaining > 0`, physics owns the player's X/Y velocity and player input is intentionally ignored — you get *shoved* across the platform instead of being able to instantly walk against the hit. Once the timer counts down, input control returns. (The interaction between this hand-off and challenge completion is what surfaced the Week 9 regression.)

**Survive-to-progress mechanic + first-pass HUD**

Stood up the core loop on `FallChallengeState`: players accrue progress for time spent surviving on the platform and the challenge advances toward completion as that progress fills. Added the initial HUD overlay on the client ImGui layer so players can actually see their survival progress fill in real time rather than guessing.

**Notifications / overlay groundwork**

Started the notifications system — pop-ups/overlays to tell players how to interact with the world or flag things of importance — on the same HUD layer the puzzle overlays use. Early but functional groundwork. Audio sourcing for hazard impacts and ambience was scoped to the artists per the team split.

#### Progress Evaluation

The week went into getting the hazard loop to *feel* fair, which was mostly a coverage and tuning problem: an even spawn distribution and sane knockback constants matter more to the experience than the raw mechanic. The trickiest non-obvious bit was the deferred-spawn requirement — emplacing onto pools mid-view-iteration is the kind of thing that works until it randomly doesn't, so buffering spawns up front saved a lot of future pain. Letting physics own velocity during knockback (rather than fighting it from the input path) made the shove feel right, though it set up the completion-state edge case I'd chase down the following week.

#### Upcoming Goals

- [ ] Finish the challenge's completion flow (participant tracking, fragment reveal, section progression)
- [ ] Replace the placeholder trigger with a platform-based activation check
- [ ] Polish the HUD into per-participant segments
- [ ] Continue the notifications/overlay system

#### Lessons Learned

ECS iteration has sharp edges: mutating the pools a view is iterating (even just emplacing new entities) can invalidate it, so deferring structural changes until after the loop is the safe pattern. Sharing a single source-of-truth constant between the physics radius and the gameplay hit radius is worth it — two "obviously equal" magic numbers will eventually disagree. And giving physics authority over velocity during a knockback window produces much better game feel than trying to override it from input every frame.

#### Individual Morale

[2/10] — Drained of mind, body, soul, and one leg (sprained on 5/25, crippled for 2–3 weeks). The pumpkins fall, even if I currently cannot stand.
### Week 5 + 6 + 7

#### Goals

- [x] Design and implement a full client-side audio engine using SoLoud
- [x] Implement 3D positional audio with distance attenuation
- [x] Implement entity-based sound emitters with layered sounds
- [x] Implement proximity-based sound fading (no hard cuts)
- [x] Implement global non-positional background music per game state
- [x] Sync global music state to newly-connected clients
- [x] Implement one-shot positional and non-positional sound events via server packets
- [x] Implement footstep sounds with random variant selection and pitch variation
- [x] Implement landing sound detection via ECS `Grounded` component backed by Jolt physics raycast
- [x] Add `SoundEmitter`, `SoundLayer`, `Grounded` components to shared ECS
- [x] Add `STATE_CHANGE` and `SOUND_EVENT` packets to the network protocol
- [x] Raise SoLoud voice limit to 32
- [x] Design and implement section barrier system for gating map progression
- [x] Add `SectionBarrierTag`, `SectionBarrierVisible`, `SectionBarrierPendingRemoval` components
- [x] Wire barrier removal to `SectionController::completed` flag
- [x] Add debug tooling for toggling barrier visibility and collision independently

#### Achieved

**Audio Engine (`AudioEngine` class — `audio_engine.h` / `audio_engine.cpp`)**

Built a client-side audio engine on top of SoLoud. The engine is owned by `ClientGame` and updated from the client render loop. The general setup is that the server decides what sounds should exist or play, and the client reacts to ECS state or network packets.

Features implemented:

- **3D positional audio**: sounds are played via `play3d` with the listener position updated each frame from the camera entity's `Position` component. SoLoud handles distance attenuation via `INVERSE_DISTANCE` rolloff with configurable min/max distances (`5.0f` to `1000.0f`). The listener up-vector is explicitly set to Z-up to match the engine coordinate system.

- **Entity sound emitter system**: entities carry a `SoundEmitter` component (up to 4 `SoundLayer`s each) serialized from the server. Each frame, `updateSoundEmitters` in `client_game.cpp` iterates all entities with `SoundEmitter` + `Position` and calls `AudioEngine::updateEmitter`. Layers support three trigger types: `ALWAYS`, `PROXIMITY`, and `ON_EVENT`. Each layer also has a `SoundPlayMode`: `POSITIONAL` (3D via `play3d`) or `AMBIENT` (non-positional via `play`).

- **Proximity fading**: sounds fade smoothly instead of abruptly stopping/restarting. Layers start at volume 0 and lerp toward a target volume each frame using `layer.fadeSpeed * dt`. For `PROXIMITY` layers, volume fades based on listener distance. This lets ambient loops keep running silently in the background and fade back in immediately when the player returns.

- **Global background music per state**: `playGlobalLoop` / `stopGlobalLoop` / `stopAllGlobalLoops` manage looping non-positional background music. On state changes (`STATE_CHANGE` packet), the client swaps music accordingly. Newly connected clients also receive the current state so the correct music starts immediately on join.

- **One-shot sound events**: `SOUND_EVENT` packets were extended with `pitch` and `positional` fields. The client dispatches either positional playback (`playSound`) or flat playback (`playNonPositionalSound`). This replaced several older sound packet variants with a single packet type.

- **Voice limit**: increased SoLoud's active voice count from 16 to 32 to avoid sounds being dropped when many ambient emitters and player sounds are active simultaneously.

- **Master volume API**: added `setMasterVolume` / `getMasterVolume` wrappers around SoLoud global volume controls.

**ECS Components (`shared/components.h`)**

Added three new shared components for audio:

- `SoundLayer` — per-layer sound config: `soundId`, `trigger` (`ALWAYS` / `PROXIMITY` / `ON_EVENT`), `playMode` (`POSITIONAL` / `AMBIENT`), `volume`, `proximityRange`, `fadeSpeed`
- `SoundEmitter` — holds up to 4 `SoundLayer`s with a `layerCount`; serialized from server to client through the existing component registry
- `Grounded` — stores `isGrounded` and `wasGrounded`, updated each tick through a Jolt downward raycast. Used for landing sound detection.

Added three new shared components for the barrier system:

- `SectionBarrierTag` — marks an entity as a section barrier and stores `sectionID` plus `halfExtents`
- `SectionBarrierVisible` — tag component indicating the barrier currently has `RenderInfo`
- `SectionBarrierPendingRemoval` — tag component added when a barrier should be removed

**Network Protocol (`shared/protocol.h`)**

- Added `STATE_CHANGE` packet type and `StateChangePacket` struct with a `GameStateType` field (`OVERWORLD` / `MAZE`)
- Extended `SoundEventPacket` with `pitch` and `positional` fields, consolidating one-shot sound playback into a single packet type

**Server-side sound logic (`server_game.cpp`, `game_state.cpp`)**

- **Footsteps**: `movement_system_for_world` tracks a per-entity `footstepTimer`. While moving and grounded, the timer increments. Every `0.4` seconds a `SOUND_EVENT` packet is broadcast with:
  - a random footstep variant
  - a random pitch between `0.9` and `1.1`
  - positional playback enabled

  The timer resets when movement stops so footsteps resume immediately once movement starts again.

- **Landing**: `update_grounded_system` runs after each `physics.step()` call. It raycasts downward using Jolt physics and updates the `Grounded` component. `movement_system_for_world` checks for the `wasGrounded=false -> isGrounded=true` transition and broadcasts a landing sound event.

- **State music**: `OverworldState::onEnter` and `MazeState::onEnter` broadcast `StateChangePacket`s to connected clients. `onConnect` also sends the current state to newly joining players.

- **Entity ambient emitters**: static entities in `initWorldEntities` can define `soundLayers` in `StaticEntityDesc`. These are serialized as `SoundEmitter` components and picked up automatically by the client audio system.

**Section Barrier System (`scene.h`, `game_state.cpp`, `shared/components.h`)**

Implemented a section barrier system to gate progression between puzzle sections. Each barrier is an `OverworldTag` entity with a static Jolt box body and a `SectionBarrierTag` identifying which section it belongs to. Barriers are spawned in `initWorldEntities` alongside other static overworld entities.

Barrier removal is component-driven: during `OverworldState::update`, completed `SectionController`s mark matching barriers with `SectionBarrierPendingRemoval`. A second pass removes the physics body and destroys the entity. Once fragment collection is connected up, progression only needs to set `SectionController::completed = true`.

Visibility toggling uses a despawn + respawn approach because `UPDATE_ENTITY` currently supports adding/replacing components but not removing them. I also added a removal pass to `cloneRegistry` so stale components don't persist across respawns.

**Debug Tooling for Barriers**

Two debug keys were added behind the existing F2 debug toggle:

- **N** — toggles barrier visibility by adding/removing `RenderInfo` and `SectionBarrierVisible`. Collision is unchanged.
- **B** — toggles barrier collision by calling `RemoveBody` / `AddBody`. Visibility is unchanged.

The two toggles are independent, so barriers can be:
- visible + solid
- invisible + solid
- visible + passable
- invisible + passable

The barrier branch was handed off to Leon and Phillip for integration with the finalized map layout and puzzle progression logic.

**Debugging / Issues Resolved**

- SoLoud init was failing with `INVALID_PARAMETER` after passing `AUTO` enum values as numeric arguments to the 5-argument `init()` overload. Fixed by switching to the no-argument `init()` and setting the voice count separately.
- 3D attenuation initially appeared broken because `set3dMinMaxDistance` was configured with an extremely small falloff range (`3.0f` to `5.0f`). Increasing it to (`5.0f`, `1000.0f`) fixed the issue.
- The listener up-vector (`set3dListenerUp`) was missing, which caused incorrect 3D attenuation behavior.
- `update_grounded_system` was running on inactive maze avatars whose physics bodies had not been added to the Jolt world. This caused incorrect landing sounds during state transitions. Fixed by early-returning when `!bodyInterface.IsAdded(bodyId)`.
- `cloneRegistry` was not removing components during the clone pass, so stale `RenderInfo` components persisted after barrier respawns. Added a removal pass before cloning synced components.
- B and N debug keys initially were not firing because the client binary had not been rebuilt after updating `input.h`.

#### Progress Evaluation

The audio system went from nothing to a usable runtime over 1.5ish weeks. Also met the goals since week 4. Most of the debugging time ended up going into SoLoud setup issues (init arguments, attenuation ranges, listener configuration) rather than gameplay-side logic.

The footstep and landing systems also took some iteration to get grounded detection behaving consistently. A larger movement refactor using `CharacterVirtual` was scoped out but deferred for now, so the current grounded detection still uses a raycast-based approach.

The barrier system also ended up touching networking synchronization more than expected because of component removal edge cases.

#### Upcoming Goals

- [ ] Wire `SOUND_EVENT` puzzle broadcasts into actual puzzle/switch/door logic when those systems exist
- [ ] Add section ambient marker entities once map layout is finalised and audio files are available
- [ ] Investigate `CharacterVirtual` as a replacement for the current rigid body player movement
- [ ] Provide actual audio assets for footsteps, landing, section ambience, and puzzle events (currently using placeholder `oof.mp3`)
- [ ] Leon/Phillip to determine real barrier positions and half-extents from the finalised map
- [ ] Wire `SectionController::completed = true` to fragment collection in the puzzle flow

#### Lessons Learned

SoLoud's 3D audio setup requires all listener vectors (position, forward, up) to be configured correctly or attenuation behaves incorrectly. Distance attenuation settings also need to match the actual scale of the game world or sounds end up effectively static.

Consolidating multiple sound packet types into a single `SOUND_EVENT` packet simplified the networking logic a lot.

The ECS `Grounded` component also ended up being useful because multiple systems (footsteps, landing sounds, future animation work) can all share the same grounded state instead of querying physics independently.

For the barrier system, the despawn/respawn approach works for now but exposed a limitation in the current `UPDATE_ENTITY` protocol since components cannot be explicitly removed yet.

#### Individual Morale

[2/10] — Audio was more annoying than I had thought. Also haven't started the 123 PA but I believe in Jacob who believes in me... meaning I believe I can do it all in one day.

### Week 4
 
#### Goals
 
- [x] Refactor physics code into a dedicated `PhysicsEngine` class
- [x] Fix all blocking PR review comments on the Jolt physics PR
- [x] Resolve merge conflicts with `main`
- [ ] Implement physics debug mode
#### Achieved
 
- Refactored Jolt physics into a dedicated `PhysicsEngine` class
  - Moved all Jolt initialization, layer interfaces, broadphase setup, allocators, and job system out of `ServerGame` into a new `PhysicsEngine` class in `physics_engine.h` / `physics_engine.cpp`
  - `ServerGame` now composes `PhysicsEngine` as a single member (`physics`)
  - Moved `createPlayerBody`, `createFloor`, and `createMeshBody` from free functions in `server_game.cpp` into methods on `PhysicsEngine`, removing all Jolt calls from `server_game.cpp`
  - Exposed public API: `physics.step(dt)`, `physics.getBodyInterface()`, `physics.destroyBody(bodyId)`
  - Removed Jolt includes from `shared/components.h` so the client build no longer depends on Jolt at all; `PhysicsBody` component stores a plain `uint32_t bodyId`
- Fixed all blocking PR review comments
  - Fixed box entity ECS position inconsistency with its Jolt body position
  - Added Jolt body cleanup (`destroyBody`) in `onDisconnect` before ECS entity is destroyed
  - Moved `physicsSystem.Update` out of `main.cpp` into `PhysicsEngine::step`
  - Fixed player spawn `Position` z coordinate to match `createPlayerBody` spawn height
  - Removed floor `RenderInfo` from server; floor physics body still created but graphics team handles floor rendering client-side; floor entity kept in ECS with only `Position` and `PhysicsBody` for consistency
  - Used `exeDir()` for asset paths instead of relative paths; moved `util.h` / `util.cpp` from `src/client/` into `src/shared/` so both client and server can use `exeDir()`
- Introduced batch static entity spawning via `scene.h` / `scene.cpp`
  - Created `StaticEntityDesc` struct with fields for position, model name, scale, optional mesh path, render flag, and half-extents
  - `spawnStaticEntities` handles wiring each entity into both ECS and Jolt, conditionally adding `RenderInfo` based on the render flag
  - Replaced individual box and bear creation code in `main.cpp` with a single `spawnStaticEntities` call
- Fixed bounding box axis swap for mesh bodies
  - `createMeshBody` now correctly swaps Y/Z half-extents to account for Assimp's Y-up convention versus the engine's Z-up convention
  - Physics box dimensions now match the visual mesh orientation
- Resolved merge conflicts with `main` across `client_game.cpp`, `client_game.h`, `server_game.cpp`, `component_registry.h`, and `main.cpp`
  - Kept `main` branch changes for client network/render registry split and `ComponentMeta` clone function
  - Kept `jolt_physics` branch changes for physics body creation and movement system signature
- Fixed duplicate `kHeldKeyScaleFactor` definition that was introduced during a merge
- Removed duplicate `clang-tidy` invocation in `format.sh` that was running without the macOS sysroot flag, causing false "file not found" errors for standard headers for macOs builds

#### Progress Evaluation
 
The bulk of this week was spent cleaning up the Jolt physics PR to get it ready to merge. The refactor into `PhysicsEngine` went smoothly and makes the codebase significantly cleaner. Addressing my teammates comments was straightforward once the class structure was in place, since most of them were about decoupling Jolt from the rest of the code, which the refactor handled naturally. Merge conflicts took some care since `main` had diverged significantly with the client rendering refactor.
 
#### Upcoming Goals
 
- [ ] Complete physics debug mode
- [ ] Investigate `CharacterVirtual` as a replacement for the raw capsule body to fix wall-sliding behavior
- [ ] Look into baking bounding boxes for mesh assets instead of loading OBJ files at runtime (need to consult Jacob)

#### Lessons Learned

Uhhh uhhhh meow meow meow meow meow. Learned to always refactor since it made the fixes much cleaner since it made it easier to address reviewer comments, e.g. the `PhysicsEngine` class created clear boundaries that made each reviewer comment easy to address. Also learned more about Assimp's coordinate system conventions and how Y-up vs Z-up affects bounding box axis ordering. Merge conflict resolution requires understanding both sides of the conflict well enough to know which changes to keep, not just which file is "newer."
 
#### Individual Morale
 
[10000000/10] — [
<pre style="white-space: pre; overflow-x: auto; font-family: monospace;">
⣷⣦⣤⣄⡀⢀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣰⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⣿⢿⣟⡿⣿⣻⢿⡿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⡀
⣿⣿⣿⣿⣿⣧⣯⣤⢃⠠⠀⠀⠀⠀⠀⠀⠀⠈⡔⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⢿⣿⣻⣽⣿⣯⣿⣯⣷⣿⣻⣟⣿⢾⣽⣳⢯⡿⣽⣳⢿⣽⡿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣇⠢⠀⠀⠀⠀⠀⠀⡀⠁⣠⣿⣿⣿⣿⣿⣿⣿⣿⣿⣻⢾⣿⣻⠿⣿⢻⠿⣿⣿⣿⣟⣿⣽⣾⣻⢾⡽⣯⣛⡷⣯⡟⣷⢿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣎⠐⠀⠀⠀⣐⣠⣦⣤⣽⣿⣿⣿⣿⣿⣿⣿⣻⣯⣟⣯⣟⡶⣫⢗⢮⡱⣃⠞⢻⣿⣿⣿⣿⣞⣯⢿⣝⡷⢯⣗⣳⢿⣹⣟⣾⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣟⠠⢈⠀⣲⣿⣿⠿⣿⣻⣿⣿⣿⣿⣿⢿⣻⣽⣷⣻⣾⣷⣿⣽⣿⣢⡕⣊⠜⡠⢘⠫⣿⣿⣿⣯⣿⣞⡽⣻⣼⢳⣯⢳⣯⣟⣾⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⡟⠤⢀⣼⣿⡱⢺⣽⣳⣿⣿⣿⣿⣯⡿⣿⢿⣳⡯⢷⣿⣿⣿⠿⣿⣷⣦⣥⡊⢔⠡⢂⠤⠉⡍⠿⣯⢿⣿⣧⢯⡷⣏⡿⣾⡽⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⢡⣿⡿⠁⢤⣹⣷⣿⣿⣿⢿⣳⡿⣽⠿⣟⡻⢿⣽⣧⠻⢿⣄⣴⢿⣿⣿⣷⣈⡜⢠⠎⠱⠀⡀⢉⠻⢿⣿⣿⣿⣽⣻⢷⣻⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⡿⣼⣿⣧⣼⣿⣿⣿⣿⣿⣽⣻⡽⣻⢭⡻⢥⡙⢣⢿⣷⡅⠢⢹⣿⣾⠿⣿⣿⢧⠼⡥⢊⠅⢂⠁⠠⠱⢆⠻⣯⣿⡾⣽⣿⣻⣽⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⣿⠷⣿⣿⣿⣿⣳⣟⣾⣳⠽⣇⢏⠲⠡⠌⢃⠮⣻⣿⣄⡙⣿⣷⣼⣿⢯⣛⢞⡳⢍⠢⢈⠤⢁⠣⠎⣴⡟⣯⢿⣿⣟⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⣿⣿⣿⣳⣟⡾⣳⢏⡿⣘⢎⠐⠁⠀⠂⠀⠹⢿⡛⠶⠿⣿⣿⣎⢻⣜⠪⡝⢋⣔⣡⣶⠶⡷⢷⣺⢟⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣟⣷⣟⣾⠷⣭⢻⡜⢧⡘⠀⠂⡀⢀⡀⣂⡉⠒⡌⠰⡐⢎⣿⣗⣎⣓⡘⢄⣢⣿⣷⢿⡹⡞⣧⢻⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠋
⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⣽⣿⡿⣯⣟⡾⣝⡻⣜⢯⡝⡦⡑⣡⠐⡀⢒⣰⠰⣈⠑⡌⢡⠜⣺⡟⣹⡙⣿⢞⣿⣿⡿⢿⠯⠷⣏⠷⣻⣿⣿⣿⣿⣿⣿⣿⣿⠿⠀⠀
⣿⣿⣿⣿⣿⣿⣿⣿⡿⣽⣿⣿⣿⣻⠾⣝⠮⣝⠞⣧⠝⣡⡱⢀⠂⠰⡀⣆⣱⢤⣃⡜⣰⢚⣼⣇⢣⠏⣜⣿⣿⣯⡷⣿⣾⣟⣦⣓⢦⡙⣿⣿⣿⣿⣿⣿⠁⠀⠄⠀
⣿⣿⣿⣿⣿⣿⣿⣿⣟⣿⣿⣿⣷⣿⢻⡭⢳⢌⣟⠤⢋⠴⣁⢎⣉⢒⡹⣿⢯⣷⣏⡝⠋⠛⠫⢐⠣⣚⣽⣿⣿⡏⢷⡉⠁⣿⣿⣿⣶⡽⣾⣿⣿⣿⣿⠇⠀⠁⠠⠀
⣿⣿⣿⣿⣿⣿⣿⣿⠛⣾⣿⣿⣿⣏⠿⡼⠥⢊⣆⣿⣿⣿⣿⣿⣿⣿⣴⣽⠾⣿⣿⣿⠇⠀⠀⠁⢎⣽⣿⣿⣻⢅⡂⣓⣲⣶⣿⣿⣿⣿⣽⣿⣿⠟⠛⠀⠀⡀⠀⠀
⣿⣿⣿⣿⣿⣿⣿⣿⣶⣿⣿⣿⣿⡞⣹⠲⣍⣷⣿⣿⣻⣟⣿⡟⣞⣿⣿⡿⣿⡼⣹⣿⣡⠀⡤⢈⠮⣿⣻⠟⢥⠒⣬⡜⣿⣿⣿⣿⣿⣿⣿⠟⠁⠀⠀⡀⠀⡀⠐⠀
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣣⢟⡥⢛⡼⢻⣷⣿⣷⣿⣷⡿⠿⣷⣤⢁⠹⠷⠷⣯⣿⣿⣯⣍⣾⠹⣿⡛⠠⢹⣷⣟⡼⣽⣻⣿⣿⣿⠃⠄⠁⡀⠂⠀⡀⠀⠀⠀
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⢧⣛⠎⣞⢩⣗⣿⣿⣿⣿⣿⣿⣷⣤⠀⠙⠶⣅⢫⡕⢮⣿⣿⣿⣿⣷⣿⡏⠀⡀⢡⢚⣿⣽⣿⣽⣿⣿⣿⣷⣦⣤⣀⠐⠀⡀⠐⡀⠀
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣟⡞⡜⢬⣸⣆⣿⣿⣏⠷⡸⢿⣿⣿⣿⣿⣄⠄⠘⢳⣮⣟⣽⣿⣿⣳⢿⣿⡀⠀⣀⠘⣆⢿⣾⣿⣿⡟⠻⣿⣿⣿⣿⠟⡀⠐⡀⠐⢠⠁
⣿⣿⣿⣿⣻⣿⣿⣿⣿⣿⣏⣾⢩⢆⣿⣿⡇⠘⣷⡅⣋⠜⢚⠻⣿⣿⣿⣿⣄⡀⢻⣟⣾⣿⣿⣟⢮⣷⠣⣀⢆⣏⣾⣿⣿⣿⣿⣿⠁⠘⣿⣿⣿⣿⣦⡐⠄⡉⠄⠂
⣿⣿⡿⣷⣻⣿⣿⣿⣿⣿⣽⡧⡗⣾⣿⡿⣇⠀⠘⢷⡠⠘⡚⢌⠛⢿⣿⣿⣿⣿⣦⣿⣿⣿⣿⣟⡧⢿⡰⣌⣿⣿⣿⣿⡟⠈⣿⣿⣧⠁⡘⢿⣿⣿⣿⣿⣶⣤⣆⣡
⣿⡿⣿⣽⢿⣿⣿⣿⣿⣿⣾⡷⣙⣿⣿⡗⡙⢆⡀⠀⠳⣄⡙⠆⣙⠂⣿⢿⣿⣿⣿⣿⣿⣿⣿⠷⣽⣶⣿⣿⣿⣿⠿⠋⠠⠁⢼⣿⣿⡇⠠⠘⢿⣿⣷⡘⠏⠿⡞⣿
⣿⣻⢷⣻⣟⣿⣿⣿⣿⣟⣿⡇⠒⠼⣿⣿⠀⠠⠙⣦⡀⠙⠶⣦⣬⡿⠃⠈⣿⣟⣿⣿⣿⣿⣟⣿⣿⣿⣿⣿⣿⠏⡐⠈⠄⡁⠘⣿⣿⣧⠀⢃⠘⣿⣿⣷⡈⠰⢀⠂
⡷⢯⡿⣽⣿⣿⣿⣿⣿⣿⣞⣿⣁⠂⡹⣿⣷⣀⡐⣂⢙⢦⣀⣀⣀⣤⣴⣿⣷⢾⣻⣿⣿⣻⣿⣿⣿⣿⣿⣿⣿⠂⠀⡁⢂⠀⠤⣿⣿⣿⠀⠈⢄⠘⣻⣿⠇⡘⠠⠈
⣟⡯⣽⣳⣿⣿⣿⣿⣿⣿⣿⣿⣧⣤⡁⠹⣿⣿⣷⣮⢞⣬⣟⣿⣿⣿⣿⡿⣯⣿⣿⣿⣿⣿⣿⣿⣿⣿⣧⢾⣿⣿⠀⠄⠂⣐⣼⣿⣿⠇⠠⢁⠀⢂⠈⢻⣿⡀⢃⠐
⣯⣟⣼⣳⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣬⣛⣿⣿⣿⣷⣿⣿⣿⣿⣽⣷⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣏⣿⣿⣦⣔⣷⣾⣿⠟⠁⠠⠐⡀⠈⠐⠠⠑⢾⡷⠂⠄
⢮⡷⣹⣳⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣿⣼⣻⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣼⣿⣿⡿⠿⠛⠀⠀⠌⠀⠄⠀⠌⠠⠁⠀⠊⠄⡑⠂
⣇⡻⣵⣻⣞⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⢛⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⡀⠀⠀⠀⠀⠠⠀⢀⠁⠀⠁⠠⢈⠁
⢲⡹⢖⡻⣞⣳⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣇⢫⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⡀⠁⠠⠀⠐⠀⠀⠀⠀⠀⠄⠁⢈⠀⠬⠀
⢧⡙⣎⠳⣍⠓⣎⠛⣟⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣽⣮⣷⣿⣿⣿⣿⣟⠋⢿⣿⣿⣿⣿⣿⣿⣿⣿⣧⣂⠀⠁⠂⠀⠀⠀⠀⠀⠀⠈⠠⠀⠂⠠⠐⠀
</pre>
]
 
---

### Week 3

#### Goals
i
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
