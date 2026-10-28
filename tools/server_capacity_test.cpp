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
#include "common/racing/RaceProtocol.h"

using namespace coop;
using namespace coop::proto;

void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct Clients {
    std::array<ENetHost*, 9> hosts{};
    std::array<ENetPeer*, 9> peers{};
    std::array<int, 9> ids{}, rosters{}, relays{};
    std::array<int, 9> balances{}, cheats{};
    std::array<street::Snapshot,9> cashStates{};
    std::array<bool, 9> cheatsAllowed{};
    std::array<std::array<int, 256>, 9> featureRelays{}, missionRelays{};
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
                        cheatsAllowed[i] = welcome.allowCheats;
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
                    } else if(msg==S_STREET_RACING){
                        auto operation=(street::ServerOp)r.U8();
                        if(operation==street::ServerOp::Snapshot)Check(cashStates[i].Read(r),"bad authoritative cash/race snapshot");
                    } else if (msg == G_PLAYER_BALANCE) {
                        Check(r.U8() == 0, "non-host balance update relayed");
                        BalanceChange change;
                        Check(change.Read(r) && change.target == 1 && change.amount == 12345, "bad balance update");
                        ++balances[i];
                    } else if (msg == G_CHEAT) {
                        ++cheats[i];
                    } else if (msg == G_MISSION_START || msg == G_CUTSCENE || msg == G_FADE || msg == G_MISSION_END) {
                        Check(r.U8() == 0 && r.U32() == 0x53574537 && r.Ok() && !r.Remaining(),
                              "bad host mission relay sender/payload");
                        Check(ev.channelID == CH_RELIABLE, "mission event lost reliable channel");
                        ++missionRelays[i][msg];
                    } else if (msg >= G_MISSION_AUDIO && msg <= G_INTERIOR_POP) {
                        Check(r.U8() == 7 && r.U32() == 0x12345678 && r.Ok(), "bad feature relay sender/payload");
                        ++featureRelays[i][msg];
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
        if (!done()) {
            for (int i = 0; i < 9; ++i) {
                int features = 0;
                for (int count : featureRelays[i]) features += count;
                std::fprintf(stderr, "client %d: welcomed=%d rejected=%d id=%d relays=%d features=%d balances=%d\n",
                             i, welcomed[i], rejected[i], ids[i], relays[i], features, balances[i]);
            }
            throw std::runtime_error("timed out waiting for server");
        }
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
            } else if (std::string(argv[3]).find("rules-") == 0) {
                const bool allowed = std::string(argv[3]) == "rules-on";
                for (int i = 0; i < 2; ++i) {
                    clients.Connect(i);
                    clients.Until([&] { return clients.welcomed[i]; });
                    Check(clients.cheatsAllowed[i] == allowed, "cheat policy not sent in welcome");
                }
                auto send = [&](int who, const Writer& packet) {
                    enet_peer_send(clients.peers[who], CH_RELIABLE,
                        enet_packet_create(packet.Data(), packet.Size(), ENET_PACKET_FLAG_RELIABLE));
                    enet_host_flush(clients.hosts[who]);
                };
                auto pump = [&] {
                    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
                    while (std::chrono::steady_clock::now() < end) {
                        clients.Poll();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                };
                Writer balance(G_PLAYER_BALANCE);
                BalanceChange{1, 12345}.Write(balance);
                send(0, balance);  // loading target: must be rejected
                pump();
                Check(clients.balances[1] == 0, "balance sent to loading player");
                Writer status(C_STATUS); status.U8(STATUS_INGAME);
                send(1, status);
                pump();
                send(1, balance);  // partner cannot administer balances
                for (int32_t amount : {-1, 1000000000}) {
                    Writer invalid(G_PLAYER_BALANCE);
                    BalanceChange{1, amount}.Write(invalid);
                    send(0, invalid);
                }
                Writer truncated(G_PLAYER_BALANCE); truncated.U8(1);
                send(0, truncated);
                pump();
                Check(clients.balances[0] == 0 && clients.balances[1] == 0, "unauthorized/invalid balance relayed");
                send(0, balance);
                pump();Check(clients.balances[1]==0,"legacy absolute balance RPC bypassed ledger");
                Writer authoritative(C_STREET_RACING);authoritative.U8((uint8_t)street::ClientOp::SetCash);
                authoritative.U64(clients.cashStates[0].sequence+1);authoritative.U8(1);authoritative.Str("capacity1");authoritative.I32(12345);
                send(1,authoritative);pump();Check(clients.cashStates[1].cash==0,"partner administered cash");
                send(0,authoritative);clients.Until([&]{return clients.cashStates[1].cash==12345;});
                Writer cheat(G_CHEAT); cheat.U8(6); cheat.U8(1);
                send(0, cheat);
                send(1, cheat);
                pump();
                Check(clients.cheats[0] == (allowed ? 1 : 0) && clients.cheats[1] == (allowed ? 1 : 0),
                      "server did not enforce cheat policy");
            } else {
                for (int i = 0; i < capacity; ++i) {
                    clients.Connect(i);
                    clients.Until([&] { return clients.welcomed[i]; });
                    Check(clients.ids[i] == i && clients.rosters[i] == i, "wrong slot/roster");
                }
                // The host's mission/cutscene events must reach every partner,
                // including the third player, without echoing to the host.
                for (auto msg : {G_MISSION_START, G_CUTSCENE, G_FADE, G_MISSION_END}) {
                    Writer mission(msg);
                    mission.U32(0x53574537);
                    enet_peer_send(clients.peers[0], CH_RELIABLE,
                        enet_packet_create(mission.Data(), mission.Size(), ENET_PACKET_FLAG_RELIABLE));
                }
                clients.Until([&] {
                    for (int i = 1; i < capacity; ++i)
                        for (auto msg : {G_MISSION_START, G_CUTSCENE, G_FADE, G_MISSION_END})
                            if (clients.missionRelays[i][msg] != 1) return false;
                    return true;
                });
                for (int count : clients.missionRelays[0]) Check(count == 0, "mission event echoed to host");
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
                    for (auto msg : {G_RACE, G_RADIO, G_PROPERTY_OWNERS, G_MISSION_AUDIO, G_GARAGE_DOOR,
                                     G_PROP, G_PED_SPEECH, G_MISSION_POLICY, G_VEH_SCRIPT, G_INTERIOR_POP,
                                     G_MAP_EDIT, G_PHOTO, G_IPL, G_HELI_SHOT, G_CONTACTS, G_GANG_WAR, G_SIDE,
                                     G_SCENE_CAM, G_TURF_WAR, G_STAT_SYNC, G_SIDE_DONE, G_BEAT, G_BEAT_SCORE,
                                     G_MISSION_IFP, G_MISSION_FACE, G_CAR_DOOR, G_NOISE, G_MISSION_HINT,
                                     G_MISSION_LOOK_AT, G_PED_NO_COLLIDE, G_PED_ATTACH, G_MISSION_LOOK, G_OBJ_HOLD}) {
                        Writer feature(msg);
                        feature.U32(0x12345678);
                        enet_peer_send(clients.peers[7], CH_RELIABLE,
                            enet_packet_create(feature.Data(), feature.Size(), ENET_PACKET_FLAG_RELIABLE));
                    }
                    clients.Until([&] {
                        // 33 distinct IDs: every client must receive each exactly once.
                        for (int i = 0; i < 7; ++i) {
                            int total = 0;
                            for (int count : clients.featureRelays[i]) {
                                Check(count <= 1, "feature message IDs collided");
                                total += count;
                            }
                            if (total != 33) return false;
                        }
                        return true;
                    });
                    for (int count : clients.featureRelays[7]) Check(count == 0, "feature relay echoed to sender");
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
