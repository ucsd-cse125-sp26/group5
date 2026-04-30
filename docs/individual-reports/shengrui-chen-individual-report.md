---
layout: page
title: Shengrui Chen — Individual Report
permalink: /project-spec/shengrui-chen-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

## Weekly Notes

## Week 4

1. What were your concrete goals for the week?  
My goal was to nail down the core **game actions** we will need early on—especially for the Winter section—and to map each action to **server-side phases** and the **entities/components** it touches. I wanted the design to be explicit enough that implementation would not drift once we start wiring gameplay into the real client and server.

2. What goals were you able to accomplish?  
I drafted an action list and a component-oriented breakdown for the Winter section, covering **hub movement** on the main map and **puzzle logic** (for example, the shared maze spirit and how inputs should behave differently from normal walking). I also synced with teammates on graphics: we identified additional needs for **render-facing** and **network-relevant** data so art and gameplay stay aligned with what the server will actually simulate and sync.

3. If the week went differently than you had planned, what were the reasons?  
The week leaned more **design-first** than code-first. We realized how much it helps to write the logic and entity relationships clearly **before** locking in implementation details. That slowed short-term coding, but it gave us a clearer picture of each function and which ECS pieces it depends on, which should pay off when we plug these behaviors into the real game loop.

4. What are your specific goals for the next week?  
This week focused on **functional** clarity; next week I want a **minimal visual pass**—for example, simple cubes or placeholders—so we can **see** triggers, pads, and state changes while testing. I also want to map the **integration workflow**: how the actions and components from this week flow from server authority into packets, entities, and what the client displays, so the team has a repeatable path from spec to playable slice.

5. What did you learn this week, if anything (and did you expect to learn it)?  
I learned that **naming actions separately** for different modes matters—for instance, moving a player in the hub versus stepping a shared spirit in the maze should be thought of as different behaviors even if they might share the same input packet at the wire level. I also got a better feel for how **cross-discipline conversations** (graphics + gameplay) surface component gaps early. I expected design work to be useful, but I did not expect how much it would clarify **where** server phase, puzzle phase, and per-entity state should live.

6. What is your individual morale (which might be different from the overall group morale)?  
Cautiously optimistic. The scope still feels large, but having a written map of actions and components makes the Winter slice feel achievable, and I am less worried about us coding ourselves into a corner. I am a bit anxious about schedule until we land the first integrated visual test, but overall morale is steady.


## Week 3

1. What were your concrete goals for the week?  
My goal this week was to make our multiplayer loop feel more stable and easier to reason about. I wanted to clean up the client/server sync path, keep the server authoritative, and make sure game-level state (not just player transform data) could be shared across clients.

2. What goals were you able to accomplish?  
I finished a full pass on run-state synchronization between server and client. I added shared packet definitions for game-state updates, wired the server to send authoritative run info, and added client handlers to apply that state locally. I also worked through startup/render issues and edge cases that caused confusing behavior when testing multiple clients.

3. If the week went differently than you had planned, what were the reasons?  
Some time went into debugging platform and integration details instead of only building features. In practice, getting networking and rendering behavior to be predictable across test setups took longer than expected, so I shifted from "add more features" to "stabilize the core loop first."

4. What are your specific goals for the next week?  
I want to move from basic sync to gameplay-facing logic: formalize run phases (`Lobby`, `InRun`, `Ended`), add section gating rules, and start puzzle-state messaging (server-authoritative, client-mirrored). I also want to keep documenting decisions so newer teammates can quickly understand where protocol changes belong versus ECS changes.

5. What did you learn this week, if anything (and did you expect to learn it)?  
I learned that defining clean boundaries early saves a lot of debugging later: protocol is the network contract, while ECS is local simulation structure. I expected networking to be tricky, but I underestimated how much clarity improves once packet meaning and authority rules are explicit.

6. What is your individual morale (which might be different from the overall group morale)?  
Cautiously optimistic. The project still has a lot left to build, but the core direction is clearer now, and I feel better after getting concrete progress on systems that everything else depends on.

