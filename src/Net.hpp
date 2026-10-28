#pragma once
// Transport layer for co-op. Two backends behind one interface:
//  - Steam: libsteam_api is loaded at runtime (no SDK needed to build). Lobbies through ISteamMatchmaking,
//    traffic through ISteamNetworkingMessages (relayed P2P). Uses the Spacewar test app (480) by default.
//  - LAN: plain UDP with a small reliable/ordered channel, LAN discovery by broadcast, or direct IP.
// The game layer above only sees peers, packets and events.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace net {

using PeerId = uint64_t; // Steam: SteamID64. LAN: synthetic id (host is always 1).

enum class Backend { None, Steam, Lan };

enum class EventType {
    Connected,     // client: joined the host (hostId() is valid now)
    ConnectFailed, // client: could not join (msg has the reason)
    Disconnected,  // client: lost the host / lobby closed (msg has the reason)
    PeerJoined,    // host: a new client can now be sent to
    PeerLeft,      // host: a client left or timed out
    LobbyList,     // lobbies() was refreshed (Steam lobby search or LAN discovery)
    JoinRequested, // Steam overlay "Join game" / +connect_lobby: game should call joinSteam(lobby)
};

struct Event {
    EventType type;
    PeerId peer = 0;
    uint64_t lobby = 0;
    std::string msg;
};

struct Packet {
    PeerId from = 0;
    std::vector<uint8_t> data;
};

struct LobbyInfo {
    Backend backend = Backend::None;
    uint64_t id = 0;        // Steam lobby id, or LAN: (ipv4 << 16) | port
    std::string name;       // host's name
    std::string status;     // free text set by the host (e.g. "IN LOBBY", "WAVE 7")
    int members = 0, maxMembers = 0;
    std::string address;    // LAN only: "a.b.c.d:port"
};

constexpr uint16_t DEFAULT_PORT = 27960;
constexpr int MAX_PLAYERS = 4;

// ---- lifecycle
// Tries to load libsteam_api and initialise Steam (sets SteamAppId=480 unless already set).
// Safe to call when Steam is missing: steamReady() just stays false. Also parses +connect_lobby <id>
// from argv and queues a JoinRequested event.
void init(int argc, char** argv);
void shutdown();
void update(); // pump Steam callbacks, sockets, resends, timeouts. Call once per frame.
bool pollEvent(Event& out);

bool steamReady();
std::string steamError();  // why Steam is unavailable (for the menu)
std::string localName();   // Steam persona name, else $USER/$USERNAME, else "DOCTOR"

// ---- sessions
bool hostSteam(int maxPlayers, bool friendsOnly); // creates a lobby; isHost() true immediately
void joinSteam(uint64_t lobbyId);
void requestSteamLobbies();                     // async, LobbyList event when done
void inviteFriends();                           // opens the Steam overlay invite dialog for the current lobby
bool hostLan(uint16_t port = DEFAULT_PORT);
void joinLan(const std::string& address);       // "host", "host:port" or "a.b.c.d"
void requestLanHosts();                         // broadcast discovery, LobbyList event as replies come in
void leave();                                   // closes/leaves whatever session is active

const std::vector<LobbyInfo>& lobbies();
void setStatus(const std::string& status, int members); // host: advertised in lobby list / discovery replies

bool active();      // hosting or connected/connecting
bool isHost();
bool connecting();  // client still joining
Backend backend();
PeerId selfId();
PeerId hostId();
std::vector<PeerId> peers(); // host: connected clients. client: {hostId()} once connected.
std::string peerName(PeerId id); // Steam friend persona name; LAN returns "" (game sends names itself)
int pingMs(PeerId id);           // best estimate, -1 if unknown

// ---- traffic
// Reliable messages are delivered once and in order per peer. Unreliable may drop or reorder.
// Keep messages under ~60 KB.
void send(PeerId to, const void* data, size_t len, bool reliable);
void broadcast(const void* data, size_t len, bool reliable); // host -> every peer
bool recv(Packet& out);

} // namespace net
