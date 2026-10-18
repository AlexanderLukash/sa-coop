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

// Packet flags for a channel: reliable channels get ENET_PACKET_FLAG_RELIABLE.
uint32_t FlagsForChannel(uint8_t channel);

inline ENetPacket* MakePacket(const void* data, size_t size, uint8_t channel) {
    return enet_packet_create(data, size, FlagsForChannel(channel));
}

inline void Send(ENetPeer* peer, uint8_t channel, const Writer& w) {
    if (!peer) return;
    enet_peer_send(peer, channel, MakePacket(w.Data(), w.Size(), channel));
}

}  // namespace coop::net
