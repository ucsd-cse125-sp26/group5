---
layout: page
title: Ziyue Liu — Individual Report
permalink: /project-spec/ziyue-liu-individual-report/
---

[← Back to Weekly Reports]({{ '/weekly-reports/' | relative_url }})

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

