// Headless ENet integration client. Run via test_server_capacity.py.
#include <enet/enet.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#include "common/api/protocol/Protocol.h"
#include "common/config/version/Version.h"

using namespace coop;
using namespace coop::proto;

void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct Clients {
    std::array<ENetHost*, 9> hosts{};
    std::array<ENetPeer*, 9> peers{};
    std::array<int, 9> ids{}, rosters{}, relays{}, raceRelays{}, radioRelays{};
    std::array<bool, 9> welcomed{}, rejected{};
    std::array<std::string, 9> reasons{};
    ENetAddress address{};
    uint32_t protocol = kProtocolVersion;

    explicit Clients(int port) {
        enet_address_set_host(&address, "127.0.0.1");
        address.port = (enet_uint16)port;
        ids.fill(-1);
    }
    ~Clients() {
        for (auto* host : hosts) if (host) enet_host_destroy(host);
    }
    void Connect(int i) {
        hosts[i] = enet_host_create(nullptr, 1, CH_COUNT, 0, 0);
        Check(hosts[i] != nullptr, "client creation failed");
        enet_host_compress_with_range_coder(hosts[i]);
        peers[i] = enet_host_connect(hosts[i], &address, CH_COUNT, 0);
        Check(peers[i] != nullptr, "connect failed");
    }
    void Poll() {
        for (int i = 0; i < 9; ++i) {
            if (!hosts[i]) continue;
            ENetEvent ev{};
            while (enet_host_service(hosts[i], &ev, 0) > 0) {
                if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                    Hello hello;
                    hello.protocol = protocol;
                    hello.version = COOP_VERSION_STR;
                    hello.name = "Capacity" + std::to_string(i);
                    hello.exeSize = kSupportedExeSize;
                    Writer w(C_HELLO);
                    hello.Write(w);
                    enet_peer_send(ev.peer, CH_RELIABLE,
                        enet_packet_create(w.Data(), w.Size(), ENET_PACKET_FLAG_RELIABLE));
                } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                    Reader r(ev.packet->data, ev.packet->dataLength);
                    auto msg = r.U8();
                    if (msg == S_WELCOME) {
                        Welcome welcome;
                        Check(welcome.Read(r), "malformed welcome");
                        ids[i] = welcome.yourId;
                        rosters[i] = (int)welcome.players.size();
                        Check(welcome.hostId == 0, "unexpected host ID");
                        for (int j = 0; j < rosters[i]; ++j)
                            Check(welcome.players[j].id == j, "incomplete welcome roster");
                        welcomed[i] = true;
                    } else if (msg == S_REJECT) {
                        r.Str();
                        reasons[i] = r.Str();
                        rejected[i] = true;
                    } else if (msg == G_RACE || msg == G_RADIO) {
                        Check(r.U8() == 7 && r.U32() == 0x12345678 && r.Ok(), "bad feature relay sender/payload");
                        ++(msg == G_RACE ? raceRelays[i] : radioRelays[i]);
                    } else if (msg == G_PLAYER_ONFOOT) {
                        Check(r.U8() == 7 && r.U32() == 0x87654321 && r.Ok(), "bad relay sender/payload");
                        ++relays[i];
                    }
                    enet_packet_destroy(ev.packet);
                }
            }
        }
    }
    template<class F> void Until(F done) {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done() && std::chrono::steady_clock::now() < end) {
            Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(done(), "timed out waiting for server");
    }
};

int main(int argc, char** argv) {
    try {
        Check(argc == 4, "usage: test PORT CAPACITY old|current");
        Check(enet_initialize() == 0, "ENet init failed");
        {
            Clients clients(std::atoi(argv[1]));
            int capacity = std::atoi(argv[2]);
            if (std::string(argv[3]) == "old") {
                clients.protocol = kProtocolVersion - 1;
                clients.Connect(0);
                clients.Until([&] { return clients.rejected[0]; });
                Check(!clients.welcomed[0] && clients.reasons[0].find("version") != std::string::npos,
                      "old protocol was not rejected");
            } else {
                for (int i = 0; i < capacity; ++i) {
                    clients.Connect(i);
                    clients.Until([&] { return clients.welcomed[i]; });
                    Check(clients.ids[i] == i && clients.rosters[i] == i, "wrong slot/roster");
                }
                if (capacity == 8) {
                    Writer w(G_PLAYER_ONFOOT);
                    w.U32(0x87654321);
                    enet_peer_send(clients.peers[7], CH_RELIABLE,
                        enet_packet_create(w.Data(), w.Size(), ENET_PACKET_FLAG_RELIABLE));
                    clients.Until([&] {
                        for (int i = 0; i < 7; ++i) if (clients.relays[i] != 1) return false;
                        return true;
                    });
                    Check(clients.relays[7] == 0, "relay echoed to sender");
                    // Both new message IDs must survive the real server's relay unchanged.
                    for (auto msg : {G_RACE, G_RADIO}) {
                        Writer feature(msg);
                        feature.U32(0x12345678);
                        enet_peer_send(clients.peers[7], CH_RELIABLE,
                            enet_packet_create(feature.Data(), feature.Size(), ENET_PACKET_FLAG_RELIABLE));
                    }
                    clients.Until([&] {
                        for (int i = 0; i < 7; ++i)
                            if (clients.raceRelays[i] != 1 || clients.radioRelays[i] != 1) return false;
                        return true;
                    });
                    Check(clients.raceRelays[7] == 0 && clients.radioRelays[7] == 0,
                          "feature relay echoed to sender");
                }
                clients.Connect(capacity);
                clients.Until([&] { return clients.rejected[capacity]; });
                Check(clients.reasons[capacity] == "the server is full", "wrong capacity rejection");
                enet_peer_disconnect(clients.peers[capacity - 1], 0);
                // Pump the graceful disconnect so the server can reuse the slot.
                auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
                while (std::chrono::steady_clock::now() < end) {
                    clients.Poll();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                enet_host_destroy(clients.hosts[capacity]);
                clients.hosts[capacity] = nullptr;
                clients.rejected[capacity] = false;
                clients.Connect(capacity);
                clients.Until([&] { return clients.welcomed[capacity]; });
                Check(clients.ids[capacity] == capacity - 1, "freed slot was not reused");
            }
        }
        enet_deinitialize();
        std::puts("capacity integration: ok");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
