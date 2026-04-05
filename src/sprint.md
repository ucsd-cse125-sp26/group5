# Sprint 1 — Network + ECS Skeleton

## Goal

4 players connect to a server. Each controls a circle via WASD.
The server is **authoritative**: it owns all game state. Clients are dumb input senders and state renderers.
Graphics are out of scope — console logs prove correctness at every step.

---

## Architecture Overview

```
┌──────────┐  InputPacket (WASD)   ┌──────────┐
│  Client   │ ───────────────────► │  Server   │
│           │                      │           │
│ ENet peer │ ◄─────────────────── │ ENet host │
│ EnTT reg  │  StatePacket (x,y…)  │ EnTT reg  │
└──────────┘                       └──────────┘
```

### Data flow per tick (server-side)

1. **Receive** — drain ENet events, map each peer → player entity, queue inputs.
2. **Simulate** — movement system reads queued inputs, updates `Position` components.
3. **Broadcast** — gather dirty positions, pack into a snapshot, send to every peer.

### Key conventions

| Concern | Decision |
|---------|----------|
| Authority | Server-authoritative. Clients never move entities locally (until prediction is added later). |
| Tick rate | Server runs a fixed 20 Hz game loop (`50 ms` per tick). |
| ENet channels | Channel 0 = reliable ordered (spawn/despawn). Channel 1 = unreliable (input & position updates). |
| Serialization | Plain `memcpy` of POD structs for now. All packets are prefixed with `PacketType` byte. |
| Entity IDs | Server assigns a monotonic `uint32_t`. Clients store a mapping `serverId → local entt::entity`. |

---

## Baby Steps

Each step is a self-contained unit that compiles, runs, and can be verified before moving on.

---

### Step 1 — ENet host/client lifecycle (no game logic)

**What to build:**

- `server/main.cpp`: Initialize ENet, create a host on port 7777 that accepts 4 peers. Enter a loop that polls `enet_host_service` with a 1000 ms timeout. Log connect/disconnect events. Clean up on Ctrl-C.
- `client/main.cpp`: Initialize ENet, create a client host, connect to `localhost:7777`. Poll events in a loop. Log success/failure. Disconnect gracefully on Ctrl-C.

**Verify:**

- Start server, then start 1 client. Server prints `"peer connected from <addr>"`, client prints `"connected to server"`.
- Kill the client. Server prints `"peer disconnected"`.
- Start 5 clients. The 5th should be rejected (max 4).

---

### Step 2 — Client sends a raw input packet

**What to build:**

- Define `InputPacket` in `shared/protocol.h`: `{ PacketType type; uint8_t keys; }` where `keys` is a bitmask (`W=0x01, A=0x02, S=0x04, D=0x08`).
- In the client loop, read keypresses from **stdin** (non-blocking or a second thread). When WASD state changes, serialize an `InputPacket` and send it via `enet_peer_send` on channel 1 (unreliable).
- Server receives the packet, deserializes, and logs `"peer 0 input: W=1 A=0 S=0 D=1"`.

**Verify:**

- Run server + 1 client. Press `w` in client terminal. Server prints the bitmask. Press `d`. Server prints updated bitmask. No crashes, no leaks.

---

### Step 3 — Server ECS: spawn an entity per connected peer

**What to build:**

- On the server, maintain an `entt::registry`.
- Define shared components in `shared/components.h`: `Position{float x, y}`, `Velocity{float vx, vy}`, `PlayerInput{uint8_t keys}`, `PeerLink{ENetPeer* peer}`.
- On connect: create an entity with `Position{0,0}`, `Velocity{0,0}`, `PlayerInput{0}`, `PeerLink{peer}`. Store a mapping `ENetPeer* → entt::entity`.
- On disconnect: destroy the entity, remove the mapping.
- Log entity creation/destruction.

**Verify:**

- Connect 2 clients. Server logs `"spawned entity 0 for peer <addr>"`, `"spawned entity 1 for peer <addr>"`.
- Disconnect one. Server logs `"destroyed entity 0"`.

---

### Step 4 — Server movement system (pure ECS, no network output yet)

**What to build:**

- Convert the server to a **fixed-timestep loop** (50 ms tick). Use `enet_host_service(host, 0)` (non-blocking) inside the loop, then run systems, then sleep remainder.
- Write `movement_system(registry, dt)`: for each entity with `PlayerInput` + `Position` + `Velocity`, set velocity based on input bitmask (speed = 100 units/s), then integrate position. Clear velocity if no key.
- When an `InputPacket` arrives, update the entity's `PlayerInput` component.
- Each tick, log positions: `"tick 42: entity 0 @ (150.0, 200.0)"`.

**Verify:**

- Connect 1 client. Hold `w` for ~2 seconds. Server position logs show Y increasing by ~100/s. Release — position freezes. Hold `a` — X decreases.

---

### Step 5 — Server broadcasts state to clients

**What to build:**

- Define `StatePacket` in `shared/protocol.h`: `{ PacketType type; uint8_t count; struct Entry { uint32_t entityId; float x, y; } entries[4]; }`.
- At the end of each server tick, build a `StatePacket` with every entity's position. Broadcast to all connected peers on channel 1 (unreliable).
- Client receives the packet, deserializes, and logs `"entity 0 @ (150.0, 200.0), entity 1 @ (0.0, 0.0)"`.

**Verify:**

- Server + 2 clients. Client A holds `w`. Both clients log the same positions for entity 0 (moving) and entity 1 (stationary).

---

### Step 6 — Client-side ECS registry (mirror server state)

**What to build:**

- Client maintains its own `entt::registry`.
- On first sight of an `entityId` in a `StatePacket`, create a local entity and map `serverId → localEntity`. Assign `Position` component.
- On subsequent updates, just update the `Position`.
- Add `SpawnPacket` / `DespawnPacket` on the reliable channel so the client knows exactly when entities appear/disappear (don't rely on position packet alone).
  - Server sends `SpawnPacket{entityId, x, y}` to **all** peers when a new player joins.
  - Server sends `DespawnPacket{entityId}` to **all** peers when a player leaves.
- Client logs `"spawned local entity for server id 1"` and `"destroyed local entity for server id 2"`.

**Verify:**

- 2 clients running. Client B joins late — both clients log the spawn. Client A disconnects — Client B logs the despawn. Positions update normally throughout.

---

### Step 7 — Assign spawn positions & player identity

**What to build:**

- Server assigns each player a spawn point (e.g., 4 corners of a 800×600 space).
- Add an `AssignPacket { uint32_t yourEntityId; }` sent to the newly connected client so it knows which entity it controls.
- Client logs `"I am entity 2"`.

**Verify:**

- 4 clients connect. Each prints a different `"I am entity N"`. Positions start at the four corners.

---

### Step 8 — Integration sanity test (4 players, full round-trip)

**What to build:**

Nothing new. This is a manual test checkpoint.

**Verify (do all of these):**

1. Start server.
2. Start 4 clients in 4 terminals.
3. Each client prints its assigned entity ID and sees 4 spawns.
4. Press WASD in client 1 — all 4 clients log matching position updates for entity 0.
5. Kill client 3 — remaining clients log a despawn for entity 2.
6. Reconnect a client — it gets a fresh entity ID, others see a spawn.
7. Kill server — all clients log disconnect within ~5 s (ENet timeout).

If all 7 checks pass, the network + ECS skeleton is done. Hand off to the graphics team to replace console logs with actual rendering.

---

## File Map (expected state after Step 8)

```
src/
├── shared/
│   ├── components.h    // Position, Velocity, PlayerInput, PeerLink
│   ├── protocol.h      // PacketType, InputPacket, SpawnPacket, DespawnPacket, StatePacket, AssignPacket
│   └── hello.cpp/.h    // (original, can remove later)
├── server/
│   └── main.cpp        // ENet host, fixed-tick loop, ECS systems, broadcast
└── client/
    └── main.cpp        // ENet client, stdin input, ECS mirror, console log
```

## Non-Goals (this sprint)

- Graphics / OpenGL rendering
- Client-side prediction or interpolation
- Authentication or lobby
- Persisted state
- Multiple packet types beyond movement
