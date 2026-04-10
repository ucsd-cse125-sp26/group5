#pragma once
#include <enet/enet.h>
#include "shared/net/packet_handler.h"
#include "shared/net/packet_utils.h"
#include "client_game.h"

class ClientNetwork {
public:
    bool connect(const char* host, uint16_t port, uint32_t timeoutMs = 5000);
    void disconnect();
    void shutdown();
    void poll(ClientGame& game);

    ENetPeer* getPeer() { return peer_; }
    net::PacketDispatcher<ClientGame>& dispatcher() { return dispatcher_; }

    template <typename T>
    void send(const T& packet, bool reliable = true, uint8_t channel = 0) {
        if (peer_) net::sendPacket(peer_, packet, reliable, channel);
    }

private:
    ENetHost* client_ = nullptr;
    ENetPeer* peer_   = nullptr;
    net::PacketDispatcher<ClientGame> dispatcher_;
};
