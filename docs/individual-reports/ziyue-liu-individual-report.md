---
layout: page
title: Ziyue Liu — Individual Report
permalink: /project-spec/ziyue-liu-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

## Week 4

1. **What were your concrete goals for the week?**  
   Investigate the smoothness/stuttering issue affecting the rendered output and determine the root cause of the frame consistency problem.

2. **What goals were you able to accomplish?**  
   I successfully found the bug causing the stuttering on macOS. I discovered that macOS implicitly turns off VSync when the client uses multithreading. I found this after profiling the code and noticing the client loop cycle was extremely short (about 0.1 ms instead of the expected ~8 ms for my refresh rate). I also implemented most of the state transition logic between the minigame and the outwatch game.

3. **If the week went differently than you had planned, what were the reasons?**  
   I realized that communicating more thoroughly could change how problems are analyzed. For example, before diving into profiling, I should have talked to Jacob since he uses Linux and did not have the same issue. That would have hinted early on that it was an OS compatibility issue rather than a code performance issue.

4. **What are your specific goals for the next week?**  
   Finish the state transition logic between the minigame and outwatch game, and ensure that the frame rate and rendering performance remain stable across the different operating systems our team is using.

5. **What did you learn this week, if anything (and did you expect to learn it)?**  
   I learned how to do end-to-end profiling to isolate and find issues, which was instrumental in figuring out the VSync bug on macOS.

6. **What is your individual morale?**  
   High. Resolving the stubborn stuttering bug was a huge relief, and we are making great progress moving into the different game states.

---

## Week 3

1. **What were your concrete goals for the week?**  
   Separate the network and rendering subsystems into two independent threads, and implement delta state updates to reduce bandwidth and improve responsiveness.

2. **What goals were you able to accomplish?**  
   Both goals were completed — the network and render threads are now running separately, and delta state is implemented and transmitting correctly.

3. **If the week went differently than you had planned, what were the reasons?**  
   The implementation itself went as planned, but I noticed the rendered output is not as smooth as expected. This is likely a synchronization issue between the two threads rather than a problem with the delta state logic itself.

4. **What are your specific goals for the next week?**  
   Investigate the smoothness issue by experimenting with different threading strategies — in particular, exploring the use of a single shared registry with a mutex lock versus the current dual-registry approach — to determine which yields better frame consistency.

5. **What did you learn this week, if anything (and did you expect to learn it)?**  
   Learned how to structure a game loop across multiple threads and the challenges that come with synchronizing shared state. The rendering artifacts from threading contention were not fully anticipated and were a useful lesson in concurrency trade-offs.

6. **What is your individual morale?**  
   Still high overall. Making progress on the threading architecture is meaningful work, and I'm curious to dig into the smoothness problem in the coming week.

---

## Week 2

1. **What were your concrete goals for the week?**  
   Implement and refactor the network layer so that other team members can create ECS structs without needing to interact directly with the networking code.

2. **What goals were you able to accomplish?**  
   Implemented a template-based network layer that handles serialization and deserialization generically, decoupling ECS struct definitions from the underlying networking details. The client-to-server transmission pipeline is now working cleanly.

3. **If the week went differently than you had planned, what were the reasons?**  
   The week went largely as planned. One small thing I would do differently in hindsight is to place the serialization logic in the client folder and the deserialization logic in the server folder, rather than keeping them in a shared location — this would make the separation of concerns clearer.

4. **What are your specific goals for the next week?**  
   Integrate the network layer further with the rest of the codebase, add key bindings and input callbacks, and continue refining the ECS-network interface.

5. **What did you learn this week, if anything (and did you expect to learn it)?**  
   Learned how to use the ENet library in depth and how to generalize serialization/deserialization using C++ templates. I expected to work with ENet but the degree to which templates simplified the API was a pleasant discovery.

6. **What is your individual morale?**  
   High. Playing with the ENet library and seeing it work successfully with different clients was genuinely fun and motivating.
