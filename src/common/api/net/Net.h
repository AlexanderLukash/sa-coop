#pragma once
#include <enet/enet.h>

#include <cstdint>
#include <vector>

#include "common/api/protocol/Protocol.h"
#include "common/lib/buffer/Buffer.h"

namespace coop::net {

// Reference-counted enet_initialize/enet_deinitialize.
bool Init();
void Shutdown();

// ENet's datagrams of up to 1392 bytes (1420 with IP and UDP) do not pass every line (a VPN, a mobile network, a router
// that drops IP fragments): a reliable packet that never gets through ends the connection 10-20 s later. The two sides
// agree on the smaller MTU of the two (the connect handshake), so either one setting this is enough.
constexpr enet_uint32 kMtu = 1200;

// Packet flags for a channel: reliable channels get ENET_PACKET_FLAG_RELIABLE.
uint32_t FlagsForChannel(uint8_t channel);

inline ENetPacket* MakePacket(const void* data, size_t size, uint8_t channel) {
    return enet_packet_create(data, size, FlagsForChannel(channel));
}

inline void Send(ENetPeer* peer, uint8_t channel, const Writer& w) {
    if (!peer) return;
    ENetPacket* p = MakePacket(w.Data(), w.Size(), channel);
    if (p && enet_peer_send(peer, channel, p) != 0) enet_packet_destroy(p);  // a peer going away takes nothing
}

}  // namespace coop::net
