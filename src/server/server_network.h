#pragma once
#include <functional>
#include <enet/enet.h>
#include "shared/net/packet_handler.h"
#include "server_game.h"

class ServerNetwork {
public:
    bool init(uint16_t port, size_t maxClients);
    void shutdown();
    void poll(ServerGame& game);

    ENetHost* getHost() { return host_; }
    net::PacketDispatcher<ServerGame>& dispatcher() { return dispatcher_; }

    using ConnectCallback    = std::function<void(ServerGame&, ENetPeer*)>;
    using DisconnectCallback = std::function<void(ServerGame&, ENetPeer*)>;
    ConnectCallback    onConnect;
    DisconnectCallback onDisconnect;

private:
    ENetHost* host_ = nullptr;
    net::PacketDispatcher<ServerGame> dispatcher_;
};
