---
layout: page
title: Shengrui Chen — Individual Report
permalink: /project-spec/shengrui-chen-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

## Weekly Notes

## Week 2

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

