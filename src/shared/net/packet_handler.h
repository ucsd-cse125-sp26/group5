#pragma once
#include <enet/enet.h>

#include <functional>
#include <unordered_map>

#include "shared/protocol.h"

namespace net {

template <typename TContext>
class PacketDispatcher {
 public:
  using Handler = std::function<void(TContext& ctx, ENetPeer* sender,
                                     const uint8_t* data, size_t length)>;

  void on(shared::PacketType type, Handler handler) {
    handlers_[type] = std::move(handler);
  }

  void dispatch(TContext& ctx, ENetPeer* sender, ENetPacket* packet) const {
    if (packet->dataLength == 0) return;
    auto type = static_cast<shared::PacketType>(packet->data[0]);
    auto it = handlers_.find(type);
    if (it != handlers_.end()) {
      it->second(ctx, sender, packet->data, packet->dataLength);
    } else {
      printf("Unhandled packet type %d\n", static_cast<int>(type));
    }
  }

 private:
  std::unordered_map<shared::PacketType, Handler> handlers_;
};

}  // namespace net
