---
layout: page
title: Final Project Review
permalink: /final-report/
---

The purpose of this document is to summarize the software design and implementation process we experienced throughout the quarter, looking back at our game concept document, project design specification, and schedule. Individual contributions are annotated by name.

---

## A. Concept, Design, and Schedule

### Game concept: To what extent did your game concept change from initial concept to what you implemented? If it did change, how did it change and why?

**Rebecca:** We spent around 2 - 3 weeks having somewhat heated discussions about how we want the concept of the game. Initially there were some disagreements with how the game should be, but with majority voting, the concept slowly fell into place. After the story is set (thanks to Shengrui and Sarah for coming up with many ideas for story and gameplay), really not much has changed.

Thinking back, it was quite interesting that we decided on this theme; it works perfectly for the final demo timeline wise.

### Design: How does your final project design compare to the initial design, and what are the reasons for the differences, if any?

**Rebecca:** The main design based off of concept did not change that much. If anything, we just had to narrow down the scope of the game. For example, as Saarah mentioned in the demo, we had to collect less items/fragments (only one per map); the map became much smaller (it was harder to model than imagined for me); game logic development only had time to support simpler and not randomized puzzles; Could not implement a great puzzle idea that we had about description merely because of the difficulty of projecting text onto surfaces. However, with all my hackathon and project cramming experience, this is within the normal range of scope shrink and is totally expected.

**Phillip:** The only things the changed at the end were the puzzles that we implemented, mainly because we realized we were low on time and needed to figure out contingency plans in case we were not able to finish all the puzzles planned. We went through a few different ideas for puzzles that may be easier/faster to implement, and we also considered making all the seasons just have a maze puzzle since that was the first puzzle we implemented, but in the end we took Alain's idea for a puzzle with falling objects and used Tim's last minute puzzle implementation about the shrinking zone that you have to stay within.

**Alain:** From the beginning, at least from a game design perspective, it stayed mostly the same, spearheaded by Rebecca's idea on having a cozy walking simulator with occasional puzzles along the way (at least that was my interpretation). The plot/theme of the story was contentious, however, and took a few weeks to resolve among the people interested in storyboarding (the engine people were excluded).

### Schedule: How does your final schedule compare with your projected schedule, and what are the reasons for the differences, if any?

**Rebecca:** To be honest, the website used a boiler plate schedule to begin with, and we really did not stick with it that much. I am personally mostly free this quarter, so I was confident I can get a lot of work done (albeit still did not reach my expectations…), and I did. I am groupmates with very competent people, so I had strong trust from the very beginning that they will be responsible with their work. The rest just fell into place.

**Jacob:** Our schedule was largely fake from week 4-10. We started a bit ahead of schedule on a bunch of the core engine (network, graphics) stuff, got behind on physics, and then largely began ignoring the schedule starting in week 3/4ish. I think we realized it was more efficient to work based on what people needed rather than what was scheduled for a given time. The schedule also allocated week 9 and 10 to be largely polish and testing, whereas it seems like most of the functionality was implemented then.

---

## B. General Questions

### Describe your development environment. What tools did you use? What was your build workflow? How did you support multiple platforms? Do you have any tips or suggestions for future groups for their development environment?

**Rebecca:** To break things down, for the art side:

Blender - Main modeling, sculpting (though the only model I sculpted, the sungod, did not make it into the final game) for assets and landscape. Workflow:

Landscape:

Came up with initial concept art -> Decided on the assets and environment -> Started initial landscape modeling and blocking -> Used blender landscape plug in to actually create the landscape -> UV unmapped and colored the landscape in procreate -> Additional texture with texture nodes for pixel and noise effect -> Baked

Assets:

Initial planning and experimentation using 2 assets document -> Hand drawn texture using procreate -> Some textures were baked, e.g. gradients -> Appended or imported into landscape.

Finalizing:

Using curves etc to align assets e.g. paths and fences to follow the landscape; Using scatter tool to scatter assets like trees; Working with game logic people to figure out where to place minigame specific platforms and assets.

We stored assets in two ways. Most assets (individual models, sounds, videos, textures) were stored in a folder on cse125. We then had rsync commands setup to sync this folder to/from cse125. Unfortunately this approach frequently hit permissions issues, and we also experienced races from people uploading and downloading files concurrently, leading to things like changes being lost. We also had maps stored in a special way, with a "mapsync" script helper that let people lock a mapfile, make changes, upload the changes, etc. Ultimately we had only one person really editing the map at a time so a lot of this wasn't needed, but it would've been extremely useful for the other assets. Unfortunately by the time we came to this revelation it was very close to the project deadline and not worth upgrading. I would recommend exploring hosting a git repository on cse125 and using it as a submodule in your main project.

Most people used vscode-based editors for development (vscode, antigravity, cursor). I used neovim + claude code, and setup the necessary LSP integration. I didn't really hear anyone have issues with this.

I setup nix for the builds and various dependencies, but I didn't make it mandatory for people to use, as in I made sure macos could build everything just by installing a bunch of homebrew packages. We also had a pretty complex CI setup that did a bunch of different kinds of builds (native windows build, cross-compiled windows, native macos, nix-based linux, not-nix linux). We ultimately ended up demoing a "by hand" build from me since it was easier and we hadn't tested extensively with using actions builds. Using actions fixed a lot of the cross-platform compatibility issues, and although there were some platform-specific issues (like the physics library causing crashes on windows), we were able to work around them. I think there were some initial issues on macos that were related to opengl versioning as well. It also helped that we used libraries that were generally cross-platform.

**Jacob:** Unfortunately with how I setup cross compiling (depended on people having nix setup) meant that I was on the critical path for any fast iteration on windows (alternatively, people could wait 7-10 minutes for windows builds from actions, which was too slow). This was largely only a problem on the last few days of the project, but I think its important to make this capability distributed across the group. I think using nix was pretty helpful, and I would recommend using it more widely to the next group.

### What group mechanics decisions worked out well, and which ones (if any) did not? Why?

**Ziyue:** Our team's asynchronous coding model worked exceptionally well. By leveraging individual AI tools, we eliminated the scheduling friction of pair programming, allowing developers to work efficiently and independently while using multi-tiered weekly meetings to lock in design consensus and assign short-term tasks.

However, the Tuesday sync meetings deteriorated after the high-level design was finalized in Week 2. Spotty attendance and a lack of structured agendas caused these sessions to lose productivity, resulting in information asymmetry. (We did not even come up with all 4 minigame before week 10)

**Alain:** Adding to what Ziyue (Tim) mentioned, our team lacked a strong process or incentive for providing regular updates and maintaining documentation. While some level of coordination was necessary to keep everyone aligned, it was often difficult to track remaining objectives or understand changes that others had made to the game logic and engine. In practice, the only reliable way to stay informed was to read pull requests, which was time-consuming and inefficient. Brief progress summaries or update reports from team members would likely have improved communication and reduced information asymmetry. Due to this, we couldn't fully flesh out our game as much as we'd wanted to, and were missing quite a few sound assets towards the final demo.

### Which aspects of the implementation were more difficult than you expected, and which were easier? Why?

**Jacob:** I think getting a minimum viable project (cube rotates + movement is network synchronized) was really easy. Unfortunately we stalled for a while around that "level" of functionality (basic movement, etc), because implementing the puzzles proved harder than expected.

### Which aspects of the project are you particularly proud of? Why?

**Jacob:** I think the fog is pretty cool. It originally looked really bad, but I got it to look way better.

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; text-align: center;">
  <div>
    <img src="{{ '/assets/weeklast/b.png' | relative_url }}" alt="Fog before">
    <em>Before</em>
  </div>
  <div>
    <img src="{{ '/assets/weeklast/a.png' | relative_url }}" alt="Fog after">
    <em>After</em>
  </div>
</div>

**Alain:** I liked it when everything worked for the falling pumpkins puzzle

### What was the most difficult software problem you faced, and how did you overcome it (if you did)?

**Shengrui:** The hardest problem we ran into was the spring tangram puzzle, where all four players work on the same board but each person can only see or move certain pieces depending on their role. That part was already tricky to keep in sync between server and clients, but we also hit weird physics bugs that we never fully understood. The same snap and collision setup would work on Mac and then break on Linux or Windows for no obvious reason. We mostly got it playable by keeping one role state on the server and fixing collision after each snap, but those cross platform physics issues ate a lot of time and we were often debugging by trial and error instead of knowing exactly why it behaved differently.

**Jacob:** We had some noticeable microstutter, which we were not able to resolve. I think it happens due to a desync of some kind between the server and client rendering, which interacts unpredictably with the display driver, vsync, and vrr. This is probably solvable but probably would've required lots of trial and error, or implementing something fancy like interpolation.

### Please detail your tool chain for modeling, exporting, and loading meshes, textures, and animations. What is your opinion of the tools you used?

We exported all our models to glb because it seems to have a mature exporter in blender. We had two ways of loading a model: loading a model as a part of the map, which created multiple entity in ECS for every model (in blender's rough hierarchy), and individual model loading which we used for the player models. These are the settings we used for the blender exporter. I don't know how that maps to the UI:

```python
bpy.ops.export_scene.gltf(
    filepath=r'''$DST''',
    export_format='GLB',
    export_yup=False,
    export_lights=True,
    export_extras=True,
    export_apply=True,
    use_visible=True,
    use_renderable=True,
)
```

We used assimp to load the models.

### Which networking, physics, audio, or GUI libraries did you use, and would you use them again if you were starting over knowing what you know now?

**Ziyue:** Network:

We used the ENet package, a reliable UDP network package, to transmit information between client and server. We designed the protocol, such that the client sends the user's actions (like mouse movement / key type) and the server processes the user's actions and syncs with the client 60 times every second. Using ENet gives us the performance bonus (since it is udp) while also gives security guarantees with its reliable option (behaving like tcp, where each packet will be received reliably, in-order). One lesson we learnt is that profiling is critical. When we refactor the client to use two threads to process network package and graphics rendering, vsync is turned off implicitly, resulting in significant stuttering. Having a simple profiling log to record cycle length really helped us identify the cause.

**Alain:** Sound & Physics:

We used JoltPhysics, a multi-core-friendly rigid body physics and collision detection library, and Soloud, an open-source C++ audio engine for games. For JoltPhysics, the documentation was extensive and the examples provided in the GitHub repository made the library relatively easy to learn. Although implementation was somewhat delayed while we worked through the examples during Weeks 2–3, integrating the physics subsystem into our engine was ultimately straightforward. Our architecture simulates client inputs within the ECS, transfers relevant state into the Jolt simulation, and then copies the updated values back into the ECS before synchronizing them with clients.

Our experience with Soloud was more mixed. As an open-source alternative to FMOD, it simplified several audio-related tasks, such as buffer management, PCM stream handling, and format decoding. However, its documentation was less comprehensive than that of other libraries we evaluated, making certain features—such as realistic sound attenuation—more difficult to implement. In hindsight, some of these challenges may have been due to limited development time rather than deficiencies in the library itself. Overall, despite these difficulties, I would likely choose both JoltPhysics and Soloud again for a future game engine project.

### If you used an LLM as part of your development process, what did you use it for? How well did it work? Do you have advice for students about using (or not using) an LLM for CSE 125?

**Rebecca:** LLM was used to refine weekly meeting notes into the actual weekly report when it's 3AM of Thursday. Fact check and de-ai is done afterwards. Also used claude to research online for blender issue, surprisingly it works fairly well most of the time. But it takes some base knowledge.

**Jacob:** I started off not really using LLMs but ramped up how much I "vibe coded" over the quarter. I exclusively used claude code, but others in my group used vscode, cursor, and antigravity. It was able to complete most of the tasks I asked it to do with minimal intervention. For example, it wrote all the imgui stuff, but it was limited somewhat when implementing features like fog because it couldn't see the game. I think it would be interesting for a future team to try and hook an LLM up into a harness that also lets it play the game, and I expect that would also make the LLMs more useful.

The tradeoff for LLMs is they can increase your velocity at the cost of understanding (both the codebase and the underlying concepts). I personally would recommend ramping up LLM use as the quarter goes by, and going full "vibe coder" at around week 8 or 9 since you'll need to get a lot done quickly in that timespan. I would have probably used LLMs less than I did in the project.

Also keep in mind that when you take the course, the state of the art will be roughly 10 months ahead of where it was today, so the models will probably have gotten better and cheaper.

**Phillip:** I used gemini, Copilot (went through a few models, or whichever one powers copilot by default), as well as Claude at the very end. It think LLMs work great as long as you use them integrated into your IDE, because that way they at least have codebase context. Claude was great towards the end when I started using it, I wouldn't have been able to get the things I got done without it. I just think that it really hinders your understanding because the LLM is able to do so much that you don't really have to understand what it's doing. You just need to test continuously to make sure what it's adding to the codebase isn't slop.

**Ziyue:** design the architecture with LLM, fully understand its intention and the architecture it proposed. Implement infrastructure yourselves. Do vibe coding if you run out of time.

**Alain:** I didn't like using llms at first. But my programming velocity was sorely lacking compare to jacob and tim, so in order to ramp up at first, I've used claude to help design and understand the architecture, then personally implement it. However, towards the end, I've started vibecoding the implementations as well.

**Shengrui:** I've used both Codex and ClaudeCode, and I think starting with "No Vibe Code" to set up the infrastructure is a solid approach. I used it to understand existing code and add my own. However, as the logic grew more complex, I needed a way to quickly test new features, so around weeks 8–9, I started using LLMs to implement demo functionality. My advice is to verify the code using simple logic flows after completing a full feature; this makes debugging much easier. There were times I rushed things, which led to issues that were hard to pin down.

### If you used an implementation language other than C++, describe the environments, libraries, and tools you used to support development in that language.

N/A

### How many lines of code did you write for your project?

We counted with cloc, run over the files tracked in git, excluding our vendored libraries. This is what cloc said:

| Language | Files | Blank | Comment | Code |
|---|---|---|---|---|
| C++ | 63 | 1,980 | 1,225 | 15,881 |
| C/C++ Header | 73 | 841 | 919 | 4,046 |
| Markdown (docs/this site) | 13 | 762 | 20 | 2,116 |
| GLSL | 26 | 195 | 272 | 1,452 |
| Python | 1 | 111 | 234 | 524 |
| CSS | 3 | 84 | 148 | 515 |
| YAML | 8 | 91 | 8 | 498 |
| CMake | 4 | 66 | 29 | 368 |
| Bourne Shell | 10 | 78 | 39 | 333 |
| Nix | 1 | 18 | 25 | 155 |
| HTML | 4 | 7 | 0 | 51 |
| DOS Batch | 1 | 4 | 4 | 24 |
| INI | 1 | 7 | 0 | 21 |
| Dockerfile | 1 | 13 | 1 | 20 |
| **Total** | **209** | **4,257** | **2,924** | **26,004** |

(cloc also counted ~5.9k lines of JSON, but those are just lockfiles like package-lock.json and flake.lock, so we excluded them.) That works out to roughly 20k lines of C++, 1.5k of shaders, ~1.9k of build/CI/asset management tooling, and ~2.1k of docs (this site). I was pretty careful about not including libraries by making sure we vendored all our dependencies in the libs/ folder as git submodules. I'd recommend most teams do this, though it does make cloning take longer.

### What lessons about group dynamics did you learn about working in such a large group over an extended period of time on a challenging project?

**Alain:** Someone should take on the role of a product manager, coordinator, or team lead to keep the project organized and maintain visibility into ongoing technical and artistic developments. At the same time, team members need to make a conscious effort to attend meetings and communicate progress regularly. Without consistent participation and coordination, information coherence is lost, leading to misalignment, duplicated effort, and reduced overall efficiency. This role requires maintaining a high-level understanding of both the technical and artistic aspects of the project so that dependencies and communication gaps can be identified early.

**Ziyue:** communication frameworks must evolve with the project lifecycle. High-level, synchronous alignment is vital early on, but during production, general meetings lose efficacy and breed information asymmetry if attendance falters.

**Phillip:** I think it is hard to make sure everyone is on the same page and hard to split work up sometimes. There are some tasks that are just better done all by 1 person, maybe because the tasks are connected or they depend on each other or they require context from other tasks. Sometimes when you do a task and you know someone else is doing another task that is related, it's very hard to just regurgitate all your information onto the other person so that they know what you have in mind and what you want them to do with their task or what could help them with their task. Or, sometimes you split tasks up and people end up stepping on each other's work. So it's really important delegating work to each person and having constant communication, either individually or during meetings to make sure everyone is on the same page.

**Rebecca:** The most prominent albeit none critical feedback we have gotten is to make sure our separately assigned work tasks can be cohesive at important points of development during our project. I think our team is full of wonderful people who are good at their respective tasks, but slightly bad at communicating and making things together. I am not the main tech person, but I was managing the website, and I made the decision to talk about the pipeline of writing to the website right after everything is done, which has proven to work nicely. As the group coordinator, I did wish our technical crew would work more closely together, but it turned out fine too as for now.

Another thing is about project ideas. At the beginning, our team experienced some friction because of undefined project scope and ideas. We decided to go with a majority vote for every major design choice. We can't have all the features everyone wants, and I am completely alright with that. But that also means some people get less portion of game elements they want in the game, and depending on what the technical crew can actually handle and create, that proportion is shifted further. Sometimes it is just unfortunate.

### Looking back over the past 10 weeks, is there anything you would do differently, and what would you do again in the same situation?

**Jacob:** I would've done more work in the start and middle of the quarter. Doing more work earlier would've finished the engine faster which would've helped unblock the implementation of puzzles, and doing more work in the middle would've made week 10 be less stressful and potentially improved the quality of the final game.

**Phillip:** I guess I just wish I had put in more time to really learn things and maybe code some stuff myself so I could get more out of it, rather than relying heavily on LLMs. I think it was just hard because the first few weeks, we didn't have enough of a structure and framework in place for the game logic people (me and Leon) to begin implementing. And towards the end I didn't really code that much myself, so that's where I could have gotten more out of it if I had coded more myself.

**Rebecca:** For obvious reasons, if I go back right now with my current amount of knowledge, I will be able to model much faster and put more assets. I would have also refined the landscape so it's easier for me to work with when putting bridges etc, and I would be able to handle the 2 full weeks I have basically wasted being anxious about the scatter object issue.

**Alain:** Be more outspoken on group dynamic issues and to also push my ideas to the group.

### Which courses at UCSD do you think best prepared you for CSE 125?

**Rebecca:** Since I am the art person of the team, I would have to say no classes. But for the context, mostly cse 120 and cse 160, helpful for getting a good view of what the project is doing. Blender is completely self taught.

**Jacob:** CSE 148, 110, and 132B were helpful insofar as they were "term group project" classes. 223B was pretty helpful because it increased the size of my brain. CSE 167 was slightly useful, but I think I forgot almost everything that would've been relevant to 125.

**Phillip:** I think any group project class was helpful. So a few CSE 190 classes,maybe ece 196, at least for me. Any class that truly forces you to work as a team and come together as a group to get things done is helpful, rather than the classes that have "groups" but the work is still doable by 1-2 people.

### What were the most valuable things that you learned in the class?

**Phillip:** I learned about ECS and how it works. Given how widely used ECS is, it's probably valuable. I also used Claude for the first time in 125, so I guess I learned how useful Claude is and why everyone uses it. That seems important too, given the direction the industry is moving.

**Jacob:** I think learning more about graphics was pretty cool. I never actually understood how rendering worked, and in my research I learned about lots of cool techniques that we didn't end up implementing like unreal engine's nanite.

**Rebecca:** 1. The modeling and brainstorming where to put what; I learned a decent amount of skill here. I think it's great. 2. It is always good to check in with our teammates to know what they are doing. Around the first few weeks, when I realize the project repo is getting increasingly complicated, I set up a meeting with our tech lead to understand what exactly those files are doing, so when I am writing things down for the group report, I would be able to ask some basic questions knowing how the project came together.

---

## C. Archiving

Our group site (including all weekly reports, the project spec, and this final report) is hosted directly on cse125.ucsd.edu, so no separate archiving is needed.

---

## D. Course Feedback

### For the pizza celebration after the demos, what did you think about having it in the B270 lab so that people can play each other's games?

**Alain:** It was great pls do again.

**Rebecca:** I think it's great! It was a lot of fun and it should definitely be a tradition.

### What advice/tips/suggestions would you give students who will take the course next year?

**Jacob:** Make sure everyone on your team can build both for their device and for windows, and make sure most people can run 4 copies of the game simultaneously at >=10fps (unless they have a potato). It is probably better to spend time in the basement working together (not necessarily on the same thing) compared to working asynchronously at your homes.

**Rebecca:**

1. For art people:

   (1) SEARCH UP BLENDER TUTORIALS AND ADD ONS!!!! There are ten tons of good tutorials on youtube out there teaching you shortcuts and exactly how to model a certain object. The blender version will matter, just be aware. A lazy engineer is a good engineer, and a lazy 3D artist who search for shortcuts whenever they can to deliver quickly, is also, a good artist. I am personally against making assets with AI; if you are looking to learn, you can use claude to troubleshoot, but probably don't generate slop. If you don't give a crap, then do your thing.

   (2) If you have an ipad, you can hand paint texture with procreate. Also tutorials on youtube for that.

   (3) ALWAYS duplicate objects before major changes. If you are a digital artist like me, you'd know to duplicate layers if you are unsure the new version would look good. If you are a programmer, well you know how important version control is. If you mess up, don't be afraid to delete the object and start over completely. UV, dirty mesh, weird topology, those will all get in your way in the most unexpected way even if you are doing the simplest thing. So don't worry. It might just take 10 experiments to get something look like how you imagine, and that's alright.

   (4) Look up references!! References are there to give you a general direction, and for you to branch off of. Obviously, making assets that look completely the same as other games would be boring, but that can serve as practice and a form of study, a way for you to understand how things look good. If you dont know how to make a cartoon corgi, just search up cartoon corgi. You don't have to copy the whole thing, but all art is built on previous existing styles and works. Don't brute force it. The best manga artists in the world still use references.

2. For team leaders: Make sure to have a good idea of what's going on with the project overall. People might just disagree with you, and that's bound to happen. Try your best to make every teammate feel included. Try your best to implement an average amount of everyone's ideas into the game. At the end of development, if you realize the scope is too big for the time, do not hesitate to talk to your teammates about narrowing the scope. My personal design principle is, youd rather make something small and good, than make something big that breaks.

### Do you have any suggestions for improving the course?

**Rebecca:** The only small thing that kinda sucks was the team matching phase. We did not really get to talk to each other, and the whole team creation just happened so quickly. We got slightly lost with what's going on the sceen.

### Any other comments or feedback?

**Alain:** Please keep this course running for future years.

---

## Final Screenshots

<div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
  <img src="{{ '/assets/weeklast/20260612_014938.png' | relative_url }}" alt="Final screenshot">
  <img src="{{ '/assets/weeklast/20260612_015128.png' | relative_url }}" alt="Final screenshot">
  <img src="{{ '/assets/weeklast/20260612_015227.png' | relative_url }}" alt="Final screenshot">
  <img src="{{ '/assets/weeklast/20260612_015413.png' | relative_url }}" alt="Final screenshot">
  <img src="{{ '/assets/weeklast/20260612_015751.png' | relative_url }}" alt="Final screenshot">
  <img src="{{ '/assets/weeklast/20260612_015932.png' | relative_url }}" alt="Final screenshot">
</div>
