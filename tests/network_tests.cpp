#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "server/server_game.h"
#include "shared/component_registry.h"
#include "shared/components.h"
#include "shared/net/packet_handler.h"
#include "shared/protocol.h"

// ── Mock components (never change — decoupled from game schema) ──────────

struct SimpleComponent {
  int32_t a;
  float b;
};

struct ComplexComponent {
  std::string name;
  std::vector<float> values;
  int32_t tag;
};

constexpr shared::ComponentTypeId CID_SIMPLE = 900;
constexpr shared::ComponentTypeId CID_COMPLEX = 901;

static shared::ComponentRegistry makeTestRegistry() {
  shared::ComponentRegistry reg;
  reg.registerComponent<SimpleComponent>(CID_SIMPLE);
  reg.registerComponent<ComplexComponent>(CID_COMPLEX);
  return reg;
}

// ── Test-local deserialize helper ────────────────────────────────────────
//
// Mirrors the wire format that serializeEntities() produces, using only
// ComponentRegistry — no dependency on client_game.cpp or real handlers.

struct DeserializedEntity {
  uint32_t entityId;
  entt::entity entity;
};

static std::vector<DeserializedEntity> deserializeEntities(
    const uint8_t* data, size_t len, entt::registry& registry,
    const shared::ComponentRegistry& compReg) {
  std::vector<DeserializedEntity> result;
  size_t offset = sizeof(shared::PacketType);

  EXPECT_GE(len, offset + sizeof(uint16_t));
  uint16_t entityCount;
  std::memcpy(&entityCount, data + offset, sizeof(uint16_t));
  offset += sizeof(uint16_t);

  for (uint16_t i = 0; i < entityCount; i++) {
    EXPECT_LE(offset + sizeof(uint32_t) + sizeof(uint16_t), len);

    uint32_t entityId;
    std::memcpy(&entityId, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    auto ent = registry.create();
    registry.emplace<shared::Entity>(ent, entityId);

    uint16_t compCount;
    std::memcpy(&compCount, data + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    for (uint16_t c = 0; c < compCount; c++) {
      EXPECT_LE(offset + sizeof(uint16_t) + sizeof(uint16_t), len);

      shared::ComponentTypeId cid;
      std::memcpy(&cid, data + offset, sizeof(uint16_t));
      offset += sizeof(uint16_t);

      uint16_t dataSize;
      std::memcpy(&dataSize, data + offset, sizeof(uint16_t));
      offset += sizeof(uint16_t);

      EXPECT_LE(offset + dataSize, len);

      auto* meta = compReg.find(cid);
      if (meta) {
        meta->deserialize(registry, ent, data + offset, dataSize);
      }
      offset += dataSize;
    }

    result.push_back({entityId, ent});
  }

  EXPECT_EQ(offset, len);
  return result;
}

// ═════════════════════════════════════════════════════════════════════════
// Category 1: PacketDispatcher Routing
// ═════════════════════════════════════════════════════════════════════════

struct MockContext {
  bool handlerCalled = false;
  shared::PacketType receivedType{};
  size_t receivedLength = 0;
};

TEST(Dispatcher, RegisteredHandlerCalled) {
  net::PacketDispatcher<MockContext> dispatcher;
  dispatcher.on(shared::PacketType::INPUT,
                [](MockContext& ctx, ENetPeer*, const uint8_t* data,
                   size_t len) {
                  ctx.handlerCalled = true;
                  ctx.receivedType = static_cast<shared::PacketType>(data[0]);
                  ctx.receivedLength = len;
                });

  uint8_t payload[] = {static_cast<uint8_t>(shared::PacketType::INPUT), 0xAB,
                       0xCD};
  ENetPacket pkt;
  pkt.data = payload;
  pkt.dataLength = sizeof(payload);

  MockContext ctx;
  dispatcher.dispatch(ctx, nullptr, &pkt);

  EXPECT_TRUE(ctx.handlerCalled);
  EXPECT_EQ(ctx.receivedType, shared::PacketType::INPUT);
  EXPECT_EQ(ctx.receivedLength, sizeof(payload));
}

TEST(Dispatcher, UnregisteredTypeNoHandler) {
  net::PacketDispatcher<MockContext> dispatcher;
  bool called = false;
  dispatcher.on(shared::PacketType::INPUT,
                [&called](MockContext&, ENetPeer*, const uint8_t*, size_t) {
                  called = true;
                });

  uint8_t payload[] = {static_cast<uint8_t>(shared::PacketType::SPAWN_ENTITY)};
  ENetPacket pkt;
  pkt.data = payload;
  pkt.dataLength = sizeof(payload);

  MockContext ctx;
  dispatcher.dispatch(ctx, nullptr, &pkt);

  EXPECT_FALSE(called);
}

TEST(Dispatcher, ZeroLengthPacketIgnored) {
  net::PacketDispatcher<MockContext> dispatcher;
  bool called = false;
  dispatcher.on(shared::PacketType::INPUT,
                [&called](MockContext&, ENetPeer*, const uint8_t*, size_t) {
                  called = true;
                });

  ENetPacket pkt;
  pkt.data = nullptr;
  pkt.dataLength = 0;

  MockContext ctx;
  dispatcher.dispatch(ctx, nullptr, &pkt);

  EXPECT_FALSE(called);
}

// ═════════════════════════════════════════════════════════════════════════
// Category 2: Full Pipe — Serialize → Deserialize Round-Trip
// ═════════════════════════════════════════════════════════════════════════

TEST(Pipe, SimpleComponentRoundTrip) {
  auto compReg = makeTestRegistry();

  entt::registry serverReg;
  auto ent = serverReg.create();
  serverReg.emplace<shared::Entity>(ent, 42u);
  serverReg.emplace<SimpleComponent>(ent, SimpleComponent{7, 3.14f});

  auto buf = serializeEntities(serverReg, compReg,
                               shared::PacketType::SPAWN_ENTITY, {ent}, false);

  entt::registry clientReg;
  auto entities = deserializeEntities(buf.data(), buf.size(), clientReg, compReg);

  ASSERT_EQ(entities.size(), 1u);
  EXPECT_EQ(entities[0].entityId, 42u);

  auto& comp = clientReg.get<SimpleComponent>(entities[0].entity);
  EXPECT_EQ(comp.a, 7);
  EXPECT_FLOAT_EQ(comp.b, 3.14f);
}

TEST(Pipe, ComplexComponentRoundTrip) {
  auto compReg = makeTestRegistry();

  entt::registry serverReg;
  auto ent = serverReg.create();
  serverReg.emplace<shared::Entity>(ent, 10u);
  serverReg.emplace<ComplexComponent>(
      ent, ComplexComponent{"hello", {1.0f, 2.0f, 3.0f}, 99});

  auto buf = serializeEntities(serverReg, compReg,
                               shared::PacketType::SPAWN_ENTITY, {ent}, false);

  entt::registry clientReg;
  auto entities = deserializeEntities(buf.data(), buf.size(), clientReg, compReg);

  ASSERT_EQ(entities.size(), 1u);
  auto& comp = clientReg.get<ComplexComponent>(entities[0].entity);
  EXPECT_EQ(comp.name, "hello");
  EXPECT_EQ(comp.values, (std::vector<float>{1.0f, 2.0f, 3.0f}));
  EXPECT_EQ(comp.tag, 99);
}

TEST(Pipe, BothComponentsRoundTrip) {
  auto compReg = makeTestRegistry();

  entt::registry serverReg;
  auto ent = serverReg.create();
  serverReg.emplace<shared::Entity>(ent, 5u);
  serverReg.emplace<SimpleComponent>(ent, SimpleComponent{100, -0.5f});
  serverReg.emplace<ComplexComponent>(
      ent, ComplexComponent{"both", {4.0f, 5.0f}, 11});

  auto buf = serializeEntities(serverReg, compReg,
                               shared::PacketType::SPAWN_ENTITY, {ent}, false);

  entt::registry clientReg;
  auto entities = deserializeEntities(buf.data(), buf.size(), clientReg, compReg);

  ASSERT_EQ(entities.size(), 1u);

  auto& s = clientReg.get<SimpleComponent>(entities[0].entity);
  EXPECT_EQ(s.a, 100);
  EXPECT_FLOAT_EQ(s.b, -0.5f);

  auto& c = clientReg.get<ComplexComponent>(entities[0].entity);
  EXPECT_EQ(c.name, "both");
  EXPECT_EQ(c.values, (std::vector<float>{4.0f, 5.0f}));
  EXPECT_EQ(c.tag, 11);
}

TEST(Pipe, MultipleEntities) {
  auto compReg = makeTestRegistry();

  entt::registry serverReg;
  auto ent1 = serverReg.create();
  serverReg.emplace<shared::Entity>(ent1, 1u);
  serverReg.emplace<SimpleComponent>(ent1, SimpleComponent{10, 1.0f});

  auto ent2 = serverReg.create();
  serverReg.emplace<shared::Entity>(ent2, 2u);
  serverReg.emplace<SimpleComponent>(ent2, SimpleComponent{20, 2.0f});

  auto ent3 = serverReg.create();
  serverReg.emplace<shared::Entity>(ent3, 3u);
  serverReg.emplace<ComplexComponent>(
      ent3, ComplexComponent{"third", {7.0f}, 33});

  auto buf = serializeEntities(
      serverReg, compReg, shared::PacketType::SPAWN_ENTITY,
      {ent1, ent2, ent3}, false);

  entt::registry clientReg;
  auto entities = deserializeEntities(buf.data(), buf.size(), clientReg, compReg);

  ASSERT_EQ(entities.size(), 3u);

  EXPECT_EQ(entities[0].entityId, 1u);
  EXPECT_EQ(clientReg.get<SimpleComponent>(entities[0].entity).a, 10);

  EXPECT_EQ(entities[1].entityId, 2u);
  EXPECT_EQ(clientReg.get<SimpleComponent>(entities[1].entity).a, 20);

  EXPECT_EQ(entities[2].entityId, 3u);
  EXPECT_EQ(clientReg.get<ComplexComponent>(entities[2].entity).name, "third");
}

TEST(Pipe, EmptyStringAndEmptyVector) {
  auto compReg = makeTestRegistry();

  entt::registry serverReg;
  auto ent = serverReg.create();
  serverReg.emplace<shared::Entity>(ent, 50u);
  serverReg.emplace<ComplexComponent>(ent, ComplexComponent{"", {}, 0});

  auto buf = serializeEntities(serverReg, compReg,
                               shared::PacketType::SPAWN_ENTITY, {ent}, false);

  entt::registry clientReg;
  auto entities = deserializeEntities(buf.data(), buf.size(), clientReg, compReg);

  ASSERT_EQ(entities.size(), 1u);
  auto& comp = clientReg.get<ComplexComponent>(entities[0].entity);
  EXPECT_EQ(comp.name, "");
  EXPECT_TRUE(comp.values.empty());
  EXPECT_EQ(comp.tag, 0);
}

TEST(Pipe, UpdateOverwritesExisting) {
  auto compReg = makeTestRegistry();

  // Initial state
  entt::registry serverReg;
  auto ent = serverReg.create();
  serverReg.emplace<shared::Entity>(ent, 1u);
  serverReg.emplace<SimpleComponent>(ent, SimpleComponent{100, 1.0f});

  auto spawnBuf = serializeEntities(
      serverReg, compReg, shared::PacketType::SPAWN_ENTITY, {ent}, false);

  entt::registry clientReg;
  auto spawned = deserializeEntities(spawnBuf.data(), spawnBuf.size(),
                                     clientReg, compReg);
  ASSERT_EQ(spawned.size(), 1u);
  EXPECT_EQ(clientReg.get<SimpleComponent>(spawned[0].entity).a, 100);

  // Mutate server-side and serialize UPDATE
  serverReg.get<SimpleComponent>(ent).a = 999;
  serverReg.get<SimpleComponent>(ent).b = -1.0f;

  auto updateBuf = serializeEntities(
      serverReg, compReg, shared::PacketType::UPDATE_ENTITY, {ent}, false);

  // Apply update to existing client entity (manually, matching entityId)
  size_t offset = sizeof(shared::PacketType);
  uint16_t entityCount;
  std::memcpy(&entityCount, updateBuf.data() + offset, sizeof(uint16_t));
  offset += sizeof(uint16_t);

  for (uint16_t i = 0; i < entityCount; i++) {
    uint32_t entityId;
    std::memcpy(&entityId, updateBuf.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    uint16_t compCount;
    std::memcpy(&compCount, updateBuf.data() + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    for (uint16_t c = 0; c < compCount; c++) {
      shared::ComponentTypeId cid;
      std::memcpy(&cid, updateBuf.data() + offset, sizeof(uint16_t));
      offset += sizeof(uint16_t);

      uint16_t dataSize;
      std::memcpy(&dataSize, updateBuf.data() + offset, sizeof(uint16_t));
      offset += sizeof(uint16_t);

      auto* meta = compReg.find(cid);
      if (meta) {
        meta->deserialize(clientReg, spawned[0].entity,
                          updateBuf.data() + offset, dataSize);
      }
      offset += dataSize;
    }
  }

  auto& comp = clientReg.get<SimpleComponent>(spawned[0].entity);
  EXPECT_EQ(comp.a, 999);
  EXPECT_FLOAT_EQ(comp.b, -1.0f);
}

TEST(Pipe, UnknownComponentIdMidStream) {
  // Register only ComplexComponent on the client side; SimpleComponent
  // appears in the buffer but is unknown — it should be skipped, and
  // ComplexComponent should still deserialize correctly.
  shared::ComponentRegistry serverReg;
  serverReg.registerComponent<SimpleComponent>(CID_SIMPLE);
  serverReg.registerComponent<ComplexComponent>(CID_COMPLEX);

  entt::registry sReg;
  auto ent = sReg.create();
  sReg.emplace<shared::Entity>(ent, 7u);
  sReg.emplace<SimpleComponent>(ent, SimpleComponent{999, 0.0f});
  sReg.emplace<ComplexComponent>(ent,
                                 ComplexComponent{"survive", {8.0f}, 55});

  auto buf = serializeEntities(sReg, serverReg,
                               shared::PacketType::SPAWN_ENTITY, {ent}, false);

  // Client only knows ComplexComponent
  shared::ComponentRegistry clientCompReg;
  clientCompReg.registerComponent<ComplexComponent>(CID_COMPLEX);

  entt::registry clientReg;
  auto entities =
      deserializeEntities(buf.data(), buf.size(), clientReg, clientCompReg);

  ASSERT_EQ(entities.size(), 1u);
  EXPECT_FALSE(clientReg.all_of<SimpleComponent>(entities[0].entity));

  auto& comp = clientReg.get<ComplexComponent>(entities[0].entity);
  EXPECT_EQ(comp.name, "survive");
  EXPECT_EQ(comp.values, (std::vector<float>{8.0f}));
  EXPECT_EQ(comp.tag, 55);
}
