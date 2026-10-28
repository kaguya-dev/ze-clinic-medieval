// Transport layer: Steam (runtime-loaded libsteam_api, lobbies + ISteamNetworkingMessages) and LAN (UDP with a
// small reliable/ordered channel and broadcast discovery). See Net.hpp for the public interface.
// Everything runs on the calling thread inside update(); there are no background threads.
#include "Net.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <random>

namespace net {
namespace {

constexpr const char* PROTO_VERSION = "zc1"; // bump when the game protocol changes (Steam lobby "ver")
constexpr const char* LOBBY_GAME_TAG = "zeclinic";
constexpr size_t MAX_MESSAGE = 60 * 1024;

uint64_t nowMs() { return SDL_GetTicks(); }

std::deque<Event> g_events;
std::deque<Packet> g_packets;
std::vector<LobbyInfo> g_lobbies; // merged view: steam results then LAN results

void pushEvent(EventType t, PeerId peer = 0, uint64_t lobby = 0, std::string msg = {}) {
    Event e;
    e.type = t;
    e.peer = peer;
    e.lobby = lobby;
    e.msg = std::move(msg);
    g_events.push_back(std::move(e));
}

void pushPacket(PeerId from, const uint8_t* p, size_t n) {
    Packet pk;
    pk.from = from;
    pk.data.assign(p, p + n);
    g_packets.push_back(std::move(pk));
}

// Uppercase ASCII, drop control chars, at most 12 UTF-8 code points (the game font is uppercase-only).
std::string cleanName(const char* s) {
    std::string out;
    if (!s) return out;
    int cps = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        unsigned char c = *p;
        if (c < 32 || c == 127) continue;
        bool cont = (c & 0xC0) == 0x80;
        if (!cont) {
            if (cps == 12) break;
            ++cps;
        }
        if (c >= 'a' && c <= 'z') c = char(c - 32);
        out.push_back(char(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// =====================================================================================================
// Steam (flat C API, loaded at runtime)
// =====================================================================================================

#if defined(_WIN32)
#pragma pack(push, 8)
#else
#pragma pack(push, 4)
#endif
struct CallbackMsg { int32_t hSteamUser; int iCallback; uint8_t* pubParam; int cubParam; uint8_t slack[32]; };
struct SteamAPICallCompleted { uint64_t hAsyncCall; int iCallback; uint32_t cubParam; };   // 703
struct LobbyCreated { int eResult; uint64_t lobby; };                                       // 513
struct LobbyEnter { uint64_t lobby; uint32_t perms; bool locked; uint32_t response; };      // 504
struct LobbyDataUpdate { uint64_t lobby; uint64_t member; uint8_t success; };               // 505
struct LobbyChatUpdate { uint64_t lobby, userChanged, makingChange; uint32_t stateChange; }; // 506
struct LobbyMatchList { uint32_t count; };                                                  // 510
struct GameLobbyJoinRequested { uint64_t lobby; uint64_t friendId; };                       // 333
#pragma pack(pop)

struct SteamNetIdentity { // SteamNetworkingIdentity, 136 bytes; only touched through the flat helpers
    int32_t type;
    int32_t size;
    union { uint64_t id64; uint8_t raw[128]; } u;
};
static_assert(sizeof(SteamNetIdentity) == 136, "SteamNetworkingIdentity size");

struct SteamNetMessage { // leading part of SteamNetworkingMessage_t
    void* data;
    int size;
    uint32_t conn;
    SteamNetIdentity peer;
};

enum { CB_LOBBY_ENTER = 504, CB_LOBBY_DATA = 505, CB_LOBBY_CHAT = 506, CB_LOBBY_CREATED = 513,
       CB_LOBBY_MATCHLIST = 510, CB_JOIN_REQUESTED = 333, CB_CALL_COMPLETED = 703,
       CB_SESSION_REQUEST = 1251, CB_SESSION_FAILED = 1252 };

constexpr int SEND_UNRELIABLE = 0, SEND_NONAGLE = 1, SEND_RELIABLE = 8, SEND_AUTORESTART = 32;

struct SteamApi {
    SDL_SharedObject* lib = nullptr;
    bool ready = false;
    std::string err;

    int (*InitFlat)(char*) = nullptr;
    bool (*Init)() = nullptr;
    void (*Shutdown)() = nullptr;
    int32_t (*GetHSteamPipe)() = nullptr;
    void (*MD_Init)() = nullptr;
    void (*MD_RunFrame)(int32_t) = nullptr;
    bool (*MD_GetNextCallback)(int32_t, CallbackMsg*) = nullptr;
    void (*MD_FreeLastCallback)(int32_t) = nullptr;
    bool (*MD_GetAPICallResult)(int32_t, uint64_t, void*, int, int, bool*) = nullptr;

    uint64_t (*CreateLobby)(void*, int, int) = nullptr;
    uint64_t (*JoinLobby)(void*, uint64_t) = nullptr;
    void (*LeaveLobby)(void*, uint64_t) = nullptr;
    int (*GetNumLobbyMembers)(void*, uint64_t) = nullptr;
    uint64_t (*GetLobbyMemberByIndex)(void*, uint64_t, int) = nullptr;
    uint64_t (*GetLobbyOwner)(void*, uint64_t) = nullptr;
    bool (*SetLobbyData)(void*, uint64_t, const char*, const char*) = nullptr;
    const char* (*GetLobbyData)(void*, uint64_t, const char*) = nullptr;
    bool (*SetLobbyJoinable)(void*, uint64_t, bool) = nullptr;
    uint64_t (*RequestLobbyList)(void*) = nullptr;
    void (*AddStringFilter)(void*, const char*, const char*, int) = nullptr;
    void (*AddDistanceFilter)(void*, int) = nullptr;
    uint64_t (*GetLobbyByIndex)(void*, int) = nullptr;
    int (*GetLobbyMemberLimit)(void*, uint64_t) = nullptr;

    const char* (*GetPersonaName)(void*) = nullptr;
    const char* (*GetFriendPersonaName)(void*, uint64_t) = nullptr;
    void (*ActivateInviteDialog)(void*, uint64_t) = nullptr;
    uint64_t (*GetSteamID)(void*) = nullptr;

    int (*SendMessageToUser)(void*, const SteamNetIdentity*, const void*, uint32_t, int, int) = nullptr;
    int (*ReceiveMessagesOnChannel)(void*, int, SteamNetMessage**, int) = nullptr;
    bool (*AcceptSessionWithUser)(void*, const SteamNetIdentity*) = nullptr;
    bool (*CloseSessionWithUser)(void*, const SteamNetIdentity*) = nullptr;
    int (*GetSessionConnectionInfo)(void*, const SteamNetIdentity*, void*, void*) = nullptr;
    void (*IdSetSteamID64)(SteamNetIdentity*, uint64_t) = nullptr;
    uint64_t (*IdGetSteamID64)(const SteamNetIdentity*) = nullptr;
    void (*MessageRelease)(SteamNetMessage*) = nullptr;
    void (*InitRelayNetworkAccess)(void*) = nullptr;

    void* mm = nullptr; // ISteamMatchmaking
    void* fr = nullptr; // ISteamFriends
    void* us = nullptr; // ISteamUser
    void* nm = nullptr; // ISteamNetworkingMessages
    void* nu = nullptr; // ISteamNetworkingUtils
    int32_t pipe = 0;
    uint64_t self = 0;

    // session
    bool hosting = false, client = false, connected = false;
    uint64_t lobby = 0, hostId = 0;
    uint64_t createCall = 0, joinCall = 0, listCall = 0;
    uint64_t joinStarted = 0;
    std::vector<PeerId> peers;
    std::vector<std::pair<uint64_t, uint64_t>> pendingSessions; // (steamid, first seen ms)
    std::vector<Packet> early; // client: messages that arrived before LobbyEnter was processed
    std::vector<LobbyInfo> found;
} st;

void rebuildLobbies();

template <class T> bool loadFn(T& fp, const char* name) {
    fp = reinterpret_cast<T>(SDL_LoadFunction(st.lib, name));
    return fp != nullptr;
}

void* loadAccessor(std::initializer_list<const char*> names) {
    for (const char* n : names) {
        auto f = reinterpret_cast<void* (*)()>(SDL_LoadFunction(st.lib, n));
        if (f) {
            if (void* p = f()) return p;
        }
    }
    return nullptr;
}

void setEnvDefault(const char* k, const char* v) {
    if (SDL_getenv_unsafe(k)) return;
#ifdef _WIN32
    _putenv_s(k, v);
    SetEnvironmentVariableA(k, v);
#else
    setenv(k, v, 0);
#endif
}

SDL_SharedObject* openSteamLib() {
#if defined(_WIN32)
    const char* libName = "steam_api64.dll";
#elif defined(__APPLE__)
    const char* libName = "libsteam_api.dylib";
#else
    const char* libName = "libsteam_api.so";
#endif
    SDL_SharedObject* lib = nullptr;
    if (const char* base = SDL_GetBasePath()) {
        std::string p = std::string(base) + libName;
        lib = SDL_LoadObject(p.c_str());
    }
    if (!lib) lib = SDL_LoadObject(libName);
    return lib;
}

SteamNetIdentity makeIdentity(uint64_t id) {
    SteamNetIdentity ident;
    std::memset(&ident, 0, sizeof ident);
    st.IdSetSteamID64(&ident, id);
    return ident;
}

void steamInit() {
    st.lib = openSteamLib();
    if (!st.lib) {
        st.err = "STEAM API LIBRARY NOT FOUND";
        return;
    }
    loadFn(st.InitFlat, "SteamAPI_InitFlat");
    loadFn(st.Init, "SteamAPI_Init");
    bool ok = loadFn(st.Shutdown, "SteamAPI_Shutdown");
    ok &= loadFn(st.GetHSteamPipe, "SteamAPI_GetHSteamPipe");
    ok &= loadFn(st.MD_Init, "SteamAPI_ManualDispatch_Init");
    ok &= loadFn(st.MD_RunFrame, "SteamAPI_ManualDispatch_RunFrame");
    ok &= loadFn(st.MD_GetNextCallback, "SteamAPI_ManualDispatch_GetNextCallback");
    ok &= loadFn(st.MD_FreeLastCallback, "SteamAPI_ManualDispatch_FreeLastCallback");
    ok &= loadFn(st.MD_GetAPICallResult, "SteamAPI_ManualDispatch_GetAPICallResult");
    ok &= loadFn(st.CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby");
    ok &= loadFn(st.JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby");
    ok &= loadFn(st.LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby");
    ok &= loadFn(st.GetNumLobbyMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    ok &= loadFn(st.GetLobbyMemberByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    ok &= loadFn(st.GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    ok &= loadFn(st.SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData");
    ok &= loadFn(st.GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData");
    ok &= loadFn(st.SetLobbyJoinable, "SteamAPI_ISteamMatchmaking_SetLobbyJoinable");
    ok &= loadFn(st.RequestLobbyList, "SteamAPI_ISteamMatchmaking_RequestLobbyList");
    ok &= loadFn(st.AddStringFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter");
    ok &= loadFn(st.AddDistanceFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter");
    ok &= loadFn(st.GetLobbyByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyByIndex");
    ok &= loadFn(st.GetLobbyMemberLimit, "SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit");
    ok &= loadFn(st.GetPersonaName, "SteamAPI_ISteamFriends_GetPersonaName");
    ok &= loadFn(st.GetFriendPersonaName, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    ok &= loadFn(st.ActivateInviteDialog, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog");
    ok &= loadFn(st.GetSteamID, "SteamAPI_ISteamUser_GetSteamID");
    ok &= loadFn(st.SendMessageToUser, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    ok &= loadFn(st.ReceiveMessagesOnChannel, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    ok &= loadFn(st.AcceptSessionWithUser, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    ok &= loadFn(st.CloseSessionWithUser, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser");
    ok &= loadFn(st.IdSetSteamID64, "SteamAPI_SteamNetworkingIdentity_SetSteamID64");
    ok &= loadFn(st.IdGetSteamID64, "SteamAPI_SteamNetworkingIdentity_GetSteamID64");
    ok &= loadFn(st.MessageRelease, "SteamAPI_SteamNetworkingMessage_t_Release");
    loadFn(st.GetSessionConnectionInfo, "SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo");
    loadFn(st.InitRelayNetworkAccess, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
    if (!ok || (!st.InitFlat && !st.Init)) {
        st.err = "STEAM API LIBRARY TOO OLD";
        SDL_UnloadObject(st.lib);
        st.lib = nullptr;
        return;
    }

    setEnvDefault("SteamAppId", "480");
    setEnvDefault("SteamGameId", "480");

    bool inited = false;
    if (st.InitFlat) {
        char msg[1024] = {0};
        int r = st.InitFlat(msg);
        inited = r == 0;
        if (!inited) {
            SDL_Log("net: SteamAPI_InitFlat failed (%d): %s", r, msg);
            st.err = r == 2 ? "STEAM NOT RUNNING" : r == 3 ? "STEAM CLIENT OUT OF DATE" : "STEAM INIT FAILED";
        }
    } else {
        inited = st.Init();
        if (!inited) st.err = "STEAM NOT RUNNING";
    }
    if (!inited) {
        SDL_UnloadObject(st.lib);
        st.lib = nullptr;
        return;
    }

    st.mm = loadAccessor({"SteamAPI_SteamMatchmaking_v009", "SteamAPI_SteamMatchmaking_v010"});
    st.fr = loadAccessor({"SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017", "SteamAPI_SteamFriends_v019"});
    st.us = loadAccessor({"SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021",
                          "SteamAPI_SteamUser_v024"});
    st.nm = loadAccessor({"SteamAPI_SteamNetworkingMessages_SteamAPI_v002",
                          "SteamAPI_SteamNetworkingMessages_SteamAPI_v003"});
    st.nu = loadAccessor({"SteamAPI_SteamNetworkingUtils_SteamAPI_v004",
                          "SteamAPI_SteamNetworkingUtils_SteamAPI_v005"});
    if (!st.mm || !st.fr || !st.us || !st.nm) {
        st.err = "STEAM INTERFACES MISSING";
        st.Shutdown();
        SDL_UnloadObject(st.lib);
        st.lib = nullptr;
        return;
    }
    st.MD_Init();
    st.pipe = st.GetHSteamPipe();
    st.self = st.GetSteamID(st.us);
    if (st.nu && st.InitRelayNetworkAccess) st.InitRelayNetworkAccess(st.nu);
    st.ready = true;
    st.err.clear();
    SDL_Log("net: Steam ready, user %llu", (unsigned long long)st.self);
}

bool steamIsMember(uint64_t id) {
    if (!st.ready || !st.lobby) return false;
    int n = st.GetNumLobbyMembers(st.mm, st.lobby);
    for (int i = 0; i < n; ++i)
        if (st.GetLobbyMemberByIndex(st.mm, st.lobby, i) == id) return true;
    return false;
}

void steamCloseSession(uint64_t id) {
    SteamNetIdentity ident = makeIdentity(id);
    st.CloseSessionWithUser(st.nm, &ident);
}

void steamSetLobbyStatus(const std::string& status, int members);
std::string g_status = "IN LOBBY";
int g_statusMembers = -1;

void steamResetSession() {
    st.hosting = st.client = st.connected = false;
    st.lobby = st.hostId = 0;
    st.createCall = st.joinCall = 0;
    st.joinStarted = 0;
    st.peers.clear();
    st.pendingSessions.clear();
    st.early.clear();
}

void steamLeave() {
    if (!st.ready) return;
    if (st.hosting || st.client) {
        for (PeerId p : st.peers) steamCloseSession(p);
        if (st.hostId && st.hostId != st.self) steamCloseSession(st.hostId);
        if (st.lobby) st.LeaveLobby(st.mm, st.lobby);
    }
    steamResetSession();
}

bool steamAddPeer(uint64_t id) {
    if (id == st.self || id == 0) return false;
    if (std::find(st.peers.begin(), st.peers.end(), id) != st.peers.end()) return false;
    st.peers.push_back(id);
    pushEvent(EventType::PeerJoined, id, st.lobby);
    return true;
}

void steamRemovePeer(uint64_t id, const char* why) {
    auto it = std::find(st.peers.begin(), st.peers.end(), id);
    if (it == st.peers.end()) return;
    st.peers.erase(it);
    steamCloseSession(id);
    pushEvent(EventType::PeerLeft, id, st.lobby, why);
}

void steamClientLost(const char* why) {
    uint64_t lobby = st.lobby;
    steamLeave();
    pushEvent(EventType::Disconnected, 0, lobby, why);
}

void steamClientFail(const char* why) {
    uint64_t lobby = st.lobby;
    steamLeave();
    pushEvent(EventType::ConnectFailed, 0, lobby, why);
}

const char* enterResponseText(uint32_t r) {
    switch (r) {
    case 2: return "LOBBY NO LONGER EXISTS";
    case 3: return "NOT ALLOWED TO JOIN";
    case 4: return "LOBBY FULL";
    case 6: return "BANNED FROM LOBBY";
    case 7: return "LIMITED STEAM ACCOUNT";
    case 10: case 11: return "BLOCKED BY A MEMBER";
    case 15: return "TOO MANY ATTEMPTS";
    default: return "COULD NOT JOIN LOBBY";
    }
}

std::string lobbyStr(uint64_t lobby, const char* key) {
    const char* s = st.GetLobbyData(st.mm, lobby, key);
    return s ? s : "";
}

void steamOnLobbyEnter(const LobbyEnter& e) {
    if (!st.client || st.connected || e.lobby != st.lobby) return; // host gets one too for its own lobby
    if (e.response != 1) {
        steamClientFail(enterResponseText(e.response));
        return;
    }
    if (lobbyStr(e.lobby, "game") != LOBBY_GAME_TAG) {
        steamClientFail("NOT A ZE CLINIC LOBBY");
        return;
    }
    if (lobbyStr(e.lobby, "ver") != PROTO_VERSION) {
        steamClientFail("GAME VERSION MISMATCH");
        return;
    }
    st.hostId = st.GetLobbyOwner(st.mm, e.lobby);
    if (!st.hostId || st.hostId == st.self) {
        steamClientFail("LOBBY HAS NO HOST");
        return;
    }
    st.connected = true;
    pushEvent(EventType::Connected, st.hostId, st.lobby);
    for (auto& p : st.early)
        if (p.from == st.hostId) g_packets.push_back(std::move(p));
    st.early.clear();
}

void steamOnLobbyCreated(const LobbyCreated& c, bool ioFailed) {
    if (!st.hosting || st.lobby) return;
    if (ioFailed || c.eResult != 1) {
        SDL_Log("net: CreateLobby failed (result %d, io %d)", c.eResult, int(ioFailed));
        steamResetSession();
        pushEvent(EventType::Disconnected, 0, 0, "COULD NOT CREATE STEAM LOBBY");
        return;
    }
    st.lobby = c.lobby;
    st.SetLobbyData(st.mm, st.lobby, "game", LOBBY_GAME_TAG);
    st.SetLobbyData(st.mm, st.lobby, "ver", PROTO_VERSION);
    st.SetLobbyData(st.mm, st.lobby, "name", localName().c_str());
    steamSetLobbyStatus(g_status, g_statusMembers);
    st.SetLobbyJoinable(st.mm, st.lobby, true);
    SDL_Log("net: Steam lobby created %llu", (unsigned long long)st.lobby);
}

void steamOnMatchList(const LobbyMatchList& m, bool ioFailed) {
    st.found.clear();
    if (!ioFailed) {
        for (uint32_t i = 0; i < m.count; ++i) {
            uint64_t id = st.GetLobbyByIndex(st.mm, int(i));
            if (!id) continue;
            LobbyInfo li;
            li.backend = Backend::Steam;
            li.id = id;
            li.name = cleanName(lobbyStr(id, "name").c_str());
            li.status = lobbyStr(id, "status");
            std::string mem = lobbyStr(id, "members");
            li.members = mem.empty() ? st.GetNumLobbyMembers(st.mm, id) : std::atoi(mem.c_str());
            li.maxMembers = st.GetLobbyMemberLimit(st.mm, id);
            st.found.push_back(std::move(li));
        }
    }
    rebuildLobbies();
    pushEvent(EventType::LobbyList);
}

void steamOnChatUpdate(const LobbyChatUpdate& u) {
    if (u.lobby != st.lobby || !st.lobby) return;
    bool entered = u.stateChange & 1;
    bool gone = u.stateChange & (2 | 4 | 8 | 16);
    if (st.hosting) {
        if (entered) steamAddPeer(u.userChanged);
        else if (gone) steamRemovePeer(u.userChanged, (u.stateChange & 2) ? "LEFT" : "DISCONNECTED");
    } else if (st.client) {
        if (gone && u.userChanged == st.self) steamClientLost("REMOVED FROM LOBBY");
        else if (gone && st.connected && u.userChanged == st.hostId) steamClientLost("HOST LEFT THE GAME");
    }
}

void steamOnLobbyData(const LobbyDataUpdate& d) {
    if (!st.client || !st.connected || d.lobby != st.lobby) return;
    uint64_t owner = st.GetLobbyOwner(st.mm, st.lobby);
    if (owner && owner != st.hostId) steamClientLost("HOST LEFT THE GAME");
}

void steamProcessPendingSessions() {
    uint64_t now = nowMs();
    for (size_t i = 0; i < st.pendingSessions.size();) {
        auto [id, t] = st.pendingSessions[i];
        bool accept = false;
        if (st.hosting) accept = steamIsMember(id);
        else if (st.client) accept = (st.connected && id == st.hostId) || steamIsMember(id);
        if (accept) {
            SteamNetIdentity ident = makeIdentity(id);
            st.AcceptSessionWithUser(st.nm, &ident);
            if (st.hosting) steamAddPeer(id);
        }
        if (accept || now - t > 15000 || (!st.hosting && !st.client)) {
            st.pendingSessions.erase(st.pendingSessions.begin() + long(i));
        } else {
            ++i;
        }
    }
}

void steamHandleCallResult(uint64_t call, int id, const uint8_t* buf, bool failed) {
    if (id == CB_LOBBY_CREATED && call != st.createCall) { // abandoned by leave(): don't stay in it
        LobbyCreated c{};
        std::memcpy(&c, buf, sizeof c);
        if (!failed && c.eResult == 1 && c.lobby && c.lobby != st.lobby) st.LeaveLobby(st.mm, c.lobby);
    } else if (id == CB_LOBBY_ENTER && call != st.joinCall) {
        LobbyEnter e{};
        std::memcpy(&e, buf, sizeof e);
        if (!failed && e.response == 1 && e.lobby && e.lobby != st.lobby) st.LeaveLobby(st.mm, e.lobby);
    } else if (id == CB_LOBBY_CREATED) {
        LobbyCreated c{};
        std::memcpy(&c, buf, sizeof c);
        st.createCall = 0;
        steamOnLobbyCreated(c, failed);
    } else if (id == CB_LOBBY_ENTER) {
        st.joinCall = 0;
        if (failed) {
            if (st.client && !st.connected) steamClientFail("COULD NOT JOIN LOBBY");
            return;
        }
        LobbyEnter e{};
        std::memcpy(&e, buf, sizeof e);
        steamOnLobbyEnter(e);
    } else if (id == CB_LOBBY_MATCHLIST && call == st.listCall) {
        LobbyMatchList m{};
        std::memcpy(&m, buf, sizeof m);
        st.listCall = 0;
        steamOnMatchList(m, failed);
    }
}

void steamPump() {
    if (!st.ready) return;
    st.MD_RunFrame(st.pipe);
    CallbackMsg cb;
    std::memset(&cb, 0, sizeof cb);
    static uint64_t tmpStore[1024]; // 8 KB, 8-byte aligned
    uint8_t* tmp = reinterpret_cast<uint8_t*>(tmpStore);
    while (st.MD_GetNextCallback(st.pipe, &cb)) {
        const uint8_t* p = cb.pubParam;
        size_t n = cb.cubParam > 0 ? size_t(cb.cubParam) : 0;
        auto get = [&](auto& out) {
            std::memset(&out, 0, sizeof out);
            std::memcpy(&out, p, std::min(n, sizeof out));
        };
        switch (cb.iCallback) {
        case CB_CALL_COMPLETED: {
            SteamAPICallCompleted c;
            get(c);
            bool failed = false;
            int sz = int(std::min<uint32_t>(c.cubParam, uint32_t(sizeof tmpStore)));
            std::memset(tmpStore, 0, sizeof tmpStore);
            bool got = st.MD_GetAPICallResult(st.pipe, c.hAsyncCall, tmp, sz, c.iCallback, &failed);
            steamHandleCallResult(c.hAsyncCall, c.iCallback, tmp, !got || failed);
            break;
        }
        case CB_LOBBY_ENTER: { LobbyEnter e; get(e); steamOnLobbyEnter(e); break; }
        case CB_LOBBY_CHAT: { LobbyChatUpdate u; get(u); steamOnChatUpdate(u); break; }
        case CB_LOBBY_DATA: { LobbyDataUpdate d; get(d); steamOnLobbyData(d); break; }
        case CB_JOIN_REQUESTED: {
            GameLobbyJoinRequested j;
            get(j);
            pushEvent(EventType::JoinRequested, j.friendId, j.lobby);
            break;
        }
        case CB_SESSION_REQUEST: {
            if (n >= sizeof(SteamNetIdentity)) {
                SteamNetIdentity ident;
                std::memcpy(&ident, p, sizeof ident);
                uint64_t id = st.IdGetSteamID64(&ident);
                if (id) st.pendingSessions.push_back({id, nowMs()});
            }
            break;
        }
        case CB_SESSION_FAILED:
            SDL_Log("net: Steam networking session failed");
            break;
        default:
            break;
        }
        st.MD_FreeLastCallback(st.pipe);
    }
    steamProcessPendingSessions();

    // join timeout (callbacks normally arrive within a second or two)
    if (st.client && !st.connected && st.joinStarted && nowMs() - st.joinStarted > 20000)
        steamClientFail("STEAM JOIN TIMED OUT");

    // incoming traffic (drained even when idle so stray messages don't pile up)
    SteamNetMessage* msgs[64];
    for (;;) {
        int got = st.ReceiveMessagesOnChannel(st.nm, 0, msgs, 64);
        if (got <= 0) break;
        for (int i = 0; i < got; ++i) {
            SteamNetMessage* m = msgs[i];
            uint64_t from = st.IdGetSteamID64(&m->peer);
            const uint8_t* d = (const uint8_t*)m->data;
            size_t len = m->size > 0 ? size_t(m->size) : 0;
            if (st.hosting) {
                bool known = std::find(st.peers.begin(), st.peers.end(), from) != st.peers.end();
                if (!known && steamIsMember(from)) {
                    steamAddPeer(from); // message overtook the LobbyChatUpdate
                    known = true;
                }
                if (known) pushPacket(from, d, len);
            } else if (st.client) {
                if (st.connected) {
                    if (from == st.hostId) pushPacket(from, d, len);
                } else if (st.early.size() < 256) {
                    Packet pk;
                    pk.from = from;
                    pk.data.assign(d, d + len);
                    st.early.push_back(std::move(pk));
                }
            }
            st.MessageRelease(m);
        }
        if (got < 64) break;
    }
}

void steamSetLobbyStatus(const std::string& status, int members) {
    if (!st.ready || !st.hosting || !st.lobby) return;
    st.SetLobbyData(st.mm, st.lobby, "status", status.c_str());
    int m = members >= 0 ? members : int(st.peers.size()) + 1;
    st.SetLobbyData(st.mm, st.lobby, "members", std::to_string(m).c_str());
}

void steamSend(PeerId to, const void* data, size_t len, bool reliable) {
    if (!st.ready || !to || to == st.self) return;
    SteamNetIdentity ident = makeIdentity(to);
    int flags = (reliable ? SEND_RELIABLE : SEND_UNRELIABLE) | SEND_NONAGLE | SEND_AUTORESTART;
    int r = st.SendMessageToUser(st.nm, &ident, data, uint32_t(len), flags, 0);
    if (r != 1) SDL_Log("net: SendMessageToUser -> %d", r);
}

// =====================================================================================================
// LAN (UDP)
// =====================================================================================================

#ifdef _WIN32
using sock_t = SOCKET;
const sock_t BADSOCK = INVALID_SOCKET;
void closeSock(sock_t s) { closesocket(s); }
bool g_wsa = false;
#else
using sock_t = int;
const sock_t BADSOCK = -1;
void closeSock(sock_t s) { close(s); }
#endif

constexpr uint8_t MAGIC[4] = {'Z', 'C', 'L', '1'};
enum PktType : uint8_t { P_CONNECT = 1, P_ACCEPT, P_REJECT, P_UNREL, P_REL, P_ACK, P_PING, P_PONG, P_BYE, P_PROBE, P_INFO };
constexpr size_t HDR = 5;
constexpr uint64_t CONNECT_RESEND_MS = 250, CONNECT_TIMEOUT_MS = 6000, HEARTBEAT_MS = 500, PEER_TIMEOUT_MS = 6000,
                   RESEND_MS = 120;

struct Writer {
    std::vector<uint8_t> b;
    explicit Writer(uint8_t type) { b.assign(MAGIC, MAGIC + 4); b.push_back(type); }
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { for (int i = 0; i < 2; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
    void bytes(const void* p, size_t n) { b.insert(b.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
    void str(const std::string& s) { size_t n = std::min<size_t>(s.size(), 255); u8(uint8_t(n)); bytes(s.data(), n); }
};

struct Reader {
    const uint8_t* p;
    size_t n, pos = HDR;
    bool ok = true;
    Reader(const uint8_t* p_, size_t n_) : p(p_), n(n_) {}
    uint64_t get(int bytes) {
        if (pos + size_t(bytes) > n) { ok = false; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) v |= uint64_t(p[pos + size_t(i)]) << (8 * i);
        pos += size_t(bytes);
        return v;
    }
    uint8_t u8() { return uint8_t(get(1)); }
    uint16_t u16() { return uint16_t(get(2)); }
    uint32_t u32() { return uint32_t(get(4)); }
    uint64_t u64() { return get(8); }
    std::string str() {
        size_t len = u8();
        if (!ok || pos + len > n) { ok = false; return {}; }
        std::string s((const char*)p + pos, len);
        pos += len;
        return s;
    }
    const uint8_t* rest() const { return p + pos; }
    size_t restLen() const { return pos <= n ? n - pos : 0; }
};

struct OutMsg {
    uint32_t seq;
    std::vector<uint8_t> pkt;
    uint64_t lastSent;
};

struct LanPeer {
    PeerId id = 0;
    sockaddr_in addr{};
    uint32_t nonce = 0;
    uint64_t lastRecv = 0, lastPing = 0;
    int rtt = -1;
    uint32_t sendSeq = 0, recvNext = 0;
    std::deque<OutMsg> unacked;
    std::map<uint32_t, std::vector<uint8_t>> ooo; // received ahead of recvNext
    bool ackPending = false;
};

struct Lan {
    sock_t sock = BADSOCK; // host: bound to the game port. client: ephemeral.
    bool hosting = false, client = false, connected = false;
    uint16_t port = 0;
    uint64_t instance = 0;
    PeerId self = 0, nextId = 2;
    std::vector<LanPeer> peers; // host: clients. client: just the host (id 1)
    // client handshake
    sockaddr_in target{};
    uint32_t nonce = 0;
    uint64_t connectStart = 0, lastConnectSend = 0;
    bool connectSent = false;
    uint64_t lastHeartbeat = 0;
    // discovery
    sock_t disc = BADSOCK;
    std::vector<LobbyInfo> found;
    std::vector<uint64_t> foundInstance;
    // debug loss
    float drop = 0;
    std::mt19937 rng{std::random_device{}()};
} lan;

bool sameAddr(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

std::string addrStr(const sockaddr_in& a) {
    uint32_t ip = ntohl(a.sin_addr.s_addr);
    char buf[32];
    SDL_snprintf(buf, sizeof buf, "%u.%u.%u.%u:%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255,
                 unsigned(ntohs(a.sin_port)));
    return buf;
}

bool netStartup() {
#ifdef _WIN32
    if (!g_wsa) {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return false;
        g_wsa = true;
    }
#endif
    return true;
}

sock_t openUdp(uint16_t port, bool broadcast) {
    if (!netStartup()) return BADSOCK;
    sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == BADSOCK) return BADSOCK;
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    // don't let ICMP "port unreachable" from a vanished peer turn into recvfrom errors
    DWORD off = FALSE, ret = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &off, sizeof off, nullptr, 0, &ret, nullptr, nullptr);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int one = 1;
    if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&one, int(sizeof one));
    int bufsz = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&bufsz, int(sizeof bufsz));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&bufsz, int(sizeof bufsz));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(s, (const sockaddr*)&a, int(sizeof a)) != 0) {
        closeSock(s);
        return BADSOCK;
    }
    return s;
}

void sendRaw(sock_t s, const sockaddr_in& to, const std::vector<uint8_t>& b) {
    if (s == BADSOCK) return;
    if (lan.drop > 0 && std::uniform_real_distribution<float>(0, 1)(lan.rng) < lan.drop) return;
    sendto(s, (const char*)b.data(), int(b.size()), 0, (const sockaddr*)&to, int(sizeof to));
}

LanPeer* lanFindById(PeerId id) {
    for (auto& p : lan.peers)
        if (p.id == id) return &p;
    return nullptr;
}

LanPeer* lanFindByAddr(const sockaddr_in& a) {
    for (auto& p : lan.peers)
        if (sameAddr(p.addr, a)) return &p;
    return nullptr;
}

void lanSendTo(LanPeer& p, const void* data, size_t len, bool reliable) {
    if (len > MAX_MESSAGE) {
        SDL_Log("net: message of %zu bytes too large, dropped", len);
        return;
    }
    if (reliable) {
        Writer w(P_REL);
        w.u32(p.sendSeq);
        w.bytes(data, len);
        OutMsg m{p.sendSeq++, std::move(w.b), nowMs()};
        sendRaw(lan.sock, p.addr, m.pkt);
        p.unacked.push_back(std::move(m));
    } else {
        Writer w(P_UNREL);
        w.bytes(data, len);
        sendRaw(lan.sock, p.addr, w.b);
    }
}

void lanSendBye(const sockaddr_in& to) {
    Writer w(P_BYE);
    for (int i = 0; i < 3; ++i) sendRaw(lan.sock, to, w.b);
}

void lanCloseSession() {
    if (lan.sock != BADSOCK) closeSock(lan.sock);
    lan.sock = BADSOCK;
    lan.hosting = lan.client = lan.connected = false;
    lan.peers.clear();
    lan.self = 0;
    lan.nextId = 2;
}

void lanLeave() {
    if (lan.sock != BADSOCK)
        for (auto& p : lan.peers) lanSendBye(p.addr);
    lanCloseSession();
}

void lanSendAck(LanPeer& p) {
    Writer w(P_ACK);
    w.u32(p.recvNext);
    uint32_t mask = 0;
    for (uint32_t i = 0; i < 32; ++i)
        if (p.ooo.count(p.recvNext + 1 + i)) mask |= 1u << i;
    w.u32(mask);
    sendRaw(lan.sock, p.addr, w.b);
    p.ackPending = false;
}

// Shared per-peer handling. Returns false if the peer said BYE.
bool lanHandlePeerPacket(LanPeer& p, uint8_t type, Reader& r) {
    uint64_t now = nowMs();
    p.lastRecv = now;
    switch (type) {
    case P_UNREL:
        pushPacket(p.id, r.rest(), r.restLen());
        break;
    case P_REL: {
        uint32_t seq = r.u32();
        if (!r.ok) break;
        p.ackPending = true;
        int32_t diff = int32_t(seq - p.recvNext);
        if (diff < 0) break; // duplicate
        if (diff == 0) {
            pushPacket(p.id, r.rest(), r.restLen());
            ++p.recvNext;
            for (auto it = p.ooo.find(p.recvNext); it != p.ooo.end(); it = p.ooo.find(p.recvNext)) {
                Packet pk;
                pk.from = p.id;
                pk.data = std::move(it->second);
                g_packets.push_back(std::move(pk));
                p.ooo.erase(it);
                ++p.recvNext;
            }
        } else if (diff < 8192 && !p.ooo.count(seq)) {
            p.ooo[seq].assign(r.rest(), r.rest() + r.restLen());
        }
        break;
    }
    case P_ACK: {
        uint32_t cum = r.u32(), mask = r.u32();
        if (!r.ok) break;
        while (!p.unacked.empty() && int32_t(p.unacked.front().seq - cum) < 0) p.unacked.pop_front();
        if (mask) {
            p.unacked.erase(std::remove_if(p.unacked.begin(), p.unacked.end(),
                                           [&](const OutMsg& m) {
                                               uint32_t off = m.seq - cum - 1;
                                               return off < 32 && (mask >> off) & 1;
                                           }),
                            p.unacked.end());
        }
        break;
    }
    case P_PING: {
        uint32_t t = r.u32();
        Writer w(P_PONG);
        w.u32(t);
        sendRaw(lan.sock, p.addr, w.b);
        break;
    }
    case P_PONG: {
        uint32_t t = r.u32();
        if (!r.ok) break;
        int sample = int(uint32_t(now) - t);
        if (sample >= 0 && sample < 10000) p.rtt = p.rtt < 0 ? sample : (p.rtt * 7 + sample) / 8;
        break;
    }
    case P_BYE:
        return false;
    default:
        break;
    }
    return true;
}

int lanAdvertisedMembers() {
    return g_statusMembers >= 0 ? g_statusMembers : int(lan.peers.size()) + 1;
}

void lanHostPacket(const sockaddr_in& from, const uint8_t* buf, size_t n) {
    uint8_t type = buf[4];
    Reader r(buf, n);
    if (type == P_PROBE) {
        Writer w(P_INFO);
        w.u64(lan.instance);
        w.u16(lan.port);
        w.u8(uint8_t(std::clamp(lanAdvertisedMembers(), 0, 255)));
        w.u8(uint8_t(MAX_PLAYERS));
        w.str(localName());
        w.str(g_status);
        w.str(PROTO_VERSION);
        sendRaw(lan.sock, from, w.b);
        return;
    }
    LanPeer* p = lanFindByAddr(from);
    if (type == P_CONNECT) {
        uint32_t nonce = r.u32();
        std::string ver = r.str();
        if (!r.ok) return;
        if (p && p->nonce != nonce) { // same address, new session: drop the old one
            PeerId old = p->id;
            lan.peers.erase(lan.peers.begin() + (p - lan.peers.data()));
            pushEvent(EventType::PeerLeft, old, 0, "RECONNECTED");
            p = nullptr;
        }
        if (!p) {
            const char* reject = nullptr;
            if (ver != PROTO_VERSION) reject = "GAME VERSION MISMATCH";
            else if (int(lan.peers.size()) >= MAX_PLAYERS - 1) reject = "LOBBY FULL";
            if (reject) {
                Writer w(P_REJECT);
                w.u32(nonce);
                w.str(reject);
                sendRaw(lan.sock, from, w.b);
                return;
            }
            LanPeer np;
            np.id = lan.nextId++;
            np.addr = from;
            np.nonce = nonce;
            np.lastRecv = nowMs();
            lan.peers.push_back(std::move(np));
            p = &lan.peers.back();
            pushEvent(EventType::PeerJoined, p->id);
        }
        p->lastRecv = nowMs();
        Writer w(P_ACCEPT);
        w.u32(nonce);
        w.u64(p->id);
        w.u64(lan.instance);
        sendRaw(lan.sock, from, w.b);
        return;
    }
    if (!p) return;
    if (!lanHandlePeerPacket(*p, type, r)) {
        PeerId id = p->id;
        lan.peers.erase(lan.peers.begin() + (p - lan.peers.data()));
        pushEvent(EventType::PeerLeft, id, 0, "LEFT");
    }
}

void lanClientLost(EventType t, const char* why) {
    lanCloseSession();
    pushEvent(t, 0, 0, why);
}

void lanClientPacket(const sockaddr_in& from, const uint8_t* buf, size_t n) {
    uint8_t type = buf[4];
    Reader r(buf, n);
    if (!lan.connected) {
        if (type == P_ACCEPT) {
            uint32_t nonce = r.u32();
            uint64_t id = r.u64();
            r.u64();
            if (!r.ok || nonce != lan.nonce) return;
            lan.connected = true;
            lan.self = id;
            LanPeer h;
            h.id = 1;
            h.addr = from; // replies may come from a different interface address than we targeted
            h.lastRecv = nowMs();
            lan.peers.push_back(std::move(h));
            pushEvent(EventType::Connected, 1);
        } else if (type == P_REJECT) {
            uint32_t nonce = r.u32();
            std::string why = r.str();
            if (!r.ok || nonce != lan.nonce) return;
            lanClientLost(EventType::ConnectFailed, why.empty() ? "CONNECTION REFUSED" : why.c_str());
        }
        return;
    }
    if (lan.peers.empty() || !sameAddr(from, lan.peers[0].addr)) return;
    if (!lanHandlePeerPacket(lan.peers[0], type, r)) lanClientLost(EventType::Disconnected, "HOST CLOSED THE GAME");
}

void lanDiscoveryPacket(const sockaddr_in& from, const uint8_t* buf, size_t n) {
    if (buf[4] != P_INFO) return;
    Reader r(buf, n);
    LobbyInfo li;
    uint64_t inst = r.u64();
    uint16_t port = r.u16();
    li.members = r.u8();
    li.maxMembers = r.u8();
    li.name = cleanName(r.str().c_str());
    li.status = r.str();
    std::string ver = r.str();
    if (!r.ok) return;
    if (ver != PROTO_VERSION) li.status = "VERSION MISMATCH";
    sockaddr_in a = from;
    a.sin_port = htons(port);
    li.backend = Backend::Lan;
    li.id = (uint64_t(ntohl(a.sin_addr.s_addr)) << 16) | port;
    li.address = addrStr(a);
    bool loopback = (ntohl(a.sin_addr.s_addr) >> 24) == 127;
    for (size_t i = 0; i < lan.found.size(); ++i) {
        if (lan.foundInstance[i] != inst) continue;
        // same host seen via loopback and via the LAN: prefer the LAN address (shareable)
        if (!loopback) lan.found[i] = li;
        else { lan.found[i].members = li.members; lan.found[i].status = li.status; }
        rebuildLobbies();
        pushEvent(EventType::LobbyList);
        return;
    }
    lan.found.push_back(li);
    lan.foundInstance.push_back(inst);
    rebuildLobbies();
    pushEvent(EventType::LobbyList);
}

template <class F> void drainSocket(sock_t& s, F&& handle) {
    static uint8_t buf[65536];
    for (int guard = 0; guard < 4096 && s != BADSOCK; ++guard) {
        sockaddr_in from{};
        socklen_t fl = socklen_t(sizeof from);
        int n = int(recvfrom(s, (char*)buf, int(sizeof buf), 0, (sockaddr*)&from, &fl));
        if (n < 0) {
#ifdef _WIN32
            int e = WSAGetLastError();
            if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue;
#endif
            break;
        }
        if (size_t(n) < HDR || std::memcmp(buf, MAGIC, 4) != 0) continue;
        handle(from, buf, size_t(n));
    }
}

void lanPump() {
    uint64_t now = nowMs();
    if (lan.hosting) {
        drainSocket(lan.sock, lanHostPacket);
    } else if (lan.client) {
        drainSocket(lan.sock, lanClientPacket);
        now = nowMs();
        if (lan.client && !lan.connected) {
            if (now - lan.connectStart > CONNECT_TIMEOUT_MS) {
                lanClientLost(EventType::ConnectFailed, "NO RESPONSE FROM HOST");
            } else if (!lan.connectSent || now - lan.lastConnectSend >= CONNECT_RESEND_MS) {
                Writer w(P_CONNECT);
                w.u32(lan.nonce);
                w.str(PROTO_VERSION);
                sendRaw(lan.sock, lan.target, w.b);
                lan.lastConnectSend = now;
                lan.connectSent = true;
            }
        }
    }
    if (lan.disc != BADSOCK) drainSocket(lan.disc, lanDiscoveryPacket);
    now = nowMs(); // handlers stamped lastRecv with fresh times

    if ((lan.hosting || lan.connected) && lan.sock != BADSOCK) {
        bool beat = now - lan.lastHeartbeat >= HEARTBEAT_MS;
        if (beat) lan.lastHeartbeat = now;
        for (size_t i = 0; i < lan.peers.size();) {
            LanPeer& p = lan.peers[i];
            if (now > p.lastRecv && now - p.lastRecv > PEER_TIMEOUT_MS) {
                if (lan.hosting) {
                    PeerId id = p.id;
                    lan.peers.erase(lan.peers.begin() + long(i));
                    pushEvent(EventType::PeerLeft, id, 0, "TIMED OUT");
                    continue;
                }
                lanClientLost(EventType::Disconnected, "CONNECTION TO HOST TIMED OUT");
                return;
            }
            if (p.ackPending) lanSendAck(p);
            int resent = 0;
            for (auto& m : p.unacked) {
                if (now - m.lastSent < RESEND_MS) continue;
                sendRaw(lan.sock, p.addr, m.pkt);
                m.lastSent = now;
                if (++resent >= 128) break;
            }
            if (beat) {
                Writer w(P_PING);
                w.u32(uint32_t(now));
                sendRaw(lan.sock, p.addr, w.b);
            }
            ++i;
        }
    }
}

void rebuildLobbies() {
    g_lobbies = st.found;
    g_lobbies.insert(g_lobbies.end(), lan.found.begin(), lan.found.end());
}

bool parseAddress(const std::string& in, sockaddr_in& out) {
    std::string host = in, port = std::to_string(DEFAULT_PORT);
    size_t c = in.rfind(':');
    if (c != std::string::npos) {
        host = in.substr(0, c);
        port = in.substr(c + 1);
        if (port.empty()) port = std::to_string(DEFAULT_PORT);
    }
    while (!host.empty() && host.front() == ' ') host.erase(host.begin());
    while (!host.empty() && host.back() == ' ') host.pop_back();
    if (host.empty()) return false;
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) return false;
    std::memcpy(&out, res->ai_addr, sizeof out);
    freeaddrinfo(res);
    return true;
}

uint32_t randomU32() {
    return uint32_t(lan.rng()) ^ uint32_t(SDL_GetPerformanceCounter());
}

bool g_inited = false;

} // namespace

// =====================================================================================================
// Public API
// =====================================================================================================

void init(int argc, char** argv) {
    if (g_inited) return;
    g_inited = true;
    if (const char* d = SDL_getenv_unsafe("ZC_NET_DROP")) {
        lan.drop = std::clamp(float(std::atof(d)), 0.0f, 1.0f);
        if (lan.drop > 0) SDL_Log("net: simulating %.0f%% outgoing LAN packet loss", lan.drop * 100);
    }
    lan.instance = (uint64_t(randomU32()) << 32) | randomU32();
    if (!SDL_getenv_unsafe("ZC_NO_STEAM")) steamInit();
    else st.err = "STEAM DISABLED";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "+connect_lobby") == 0) {
            uint64_t id = std::strtoull(argv[i + 1], nullptr, 10);
            if (id) pushEvent(EventType::JoinRequested, 0, id);
        }
    }
}

void shutdown() {
    leave();
    if (lan.disc != BADSOCK) closeSock(lan.disc);
    lan.disc = BADSOCK;
    if (st.ready) {
        st.Shutdown();
        st.ready = false;
    }
    if (st.lib) SDL_UnloadObject(st.lib);
    st.lib = nullptr;
#ifdef _WIN32
    if (g_wsa) WSACleanup();
    g_wsa = false;
#endif
    g_events.clear();
    g_packets.clear();
    g_inited = false;
}

void update() {
    steamPump();
    lanPump();
}

bool pollEvent(Event& out) {
    if (g_events.empty()) return false;
    out = std::move(g_events.front());
    g_events.pop_front();
    return true;
}

bool steamReady() { return st.ready; }
std::string steamError() { return st.err; }

std::string localName() {
    std::string n;
    if (st.ready) n = cleanName(st.GetPersonaName(st.fr));
    if (n.empty()) n = cleanName(SDL_getenv_unsafe("USER"));
    if (n.empty()) n = cleanName(SDL_getenv_unsafe("USERNAME"));
    if (n.empty()) n = "DOCTOR";
    return n;
}

bool hostSteam(int maxPlayers, bool friendsOnly) {
    if (!st.ready) return false;
    leave();
    int maxp = std::clamp(maxPlayers, 2, MAX_PLAYERS);
    st.createCall = st.CreateLobby(st.mm, friendsOnly ? 1 : 2, maxp);
    if (!st.createCall) return false;
    st.hosting = true;
    return true;
}

void joinSteam(uint64_t lobbyId) {
    leave();
    if (!st.ready) {
        pushEvent(EventType::ConnectFailed, 0, lobbyId, st.err.empty() ? "STEAM NOT AVAILABLE" : st.err);
        return;
    }
    st.client = true;
    st.lobby = lobbyId;
    st.joinStarted = nowMs();
    st.joinCall = st.JoinLobby(st.mm, lobbyId);
    if (!st.joinCall) steamClientFail("COULD NOT JOIN LOBBY");
}

void requestSteamLobbies() {
    if (!st.ready) {
        st.found.clear();
        rebuildLobbies();
        pushEvent(EventType::LobbyList);
        return;
    }
    st.AddStringFilter(st.mm, "game", LOBBY_GAME_TAG, 0);
    st.AddStringFilter(st.mm, "ver", PROTO_VERSION, 0);
    st.AddDistanceFilter(st.mm, 3);
    st.listCall = st.RequestLobbyList(st.mm);
}

void inviteFriends() {
    if (st.ready && st.lobby && (st.hosting || st.connected)) st.ActivateInviteDialog(st.fr, st.lobby);
}

bool hostLan(uint16_t port) {
    leave();
    sock_t s = openUdp(port, true);
    if (s == BADSOCK) {
        SDL_Log("net: could not bind UDP port %u", unsigned(port));
        return false;
    }
    lan.sock = s;
    lan.hosting = true;
    lan.port = port;
    lan.self = 1;
    lan.nextId = 2;
    lan.lastHeartbeat = nowMs();
    return true;
}

void joinLan(const std::string& address) {
    leave();
    sockaddr_in a{};
    if (!netStartup() || !parseAddress(address, a)) {
        pushEvent(EventType::ConnectFailed, 0, 0, "COULD NOT RESOLVE HOST");
        return;
    }
    sock_t s = openUdp(0, false);
    if (s == BADSOCK) {
        pushEvent(EventType::ConnectFailed, 0, 0, "COULD NOT OPEN SOCKET");
        return;
    }
    lan.sock = s;
    lan.client = true;
    lan.connected = false;
    lan.target = a;
    lan.nonce = randomU32() | 1;
    lan.connectStart = nowMs();
    lan.lastConnectSend = 0;
    lan.connectSent = false;
    lan.lastHeartbeat = nowMs();
}

void requestLanHosts() {
    if (lan.disc == BADSOCK) lan.disc = openUdp(0, true);
    lan.found.clear();
    lan.foundInstance.clear();
    rebuildLobbies();
    if (lan.disc == BADSOCK) return;
    Writer w(P_PROBE);
    w.u32(randomU32());
    std::vector<uint32_t> targets = {INADDR_BROADCAST, INADDR_LOOPBACK};
#ifndef _WIN32
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (ifaddrs* i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            if (!(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr) continue;
            uint32_t b = ntohl(((sockaddr_in*)i->ifa_broadaddr)->sin_addr.s_addr);
            if (std::find(targets.begin(), targets.end(), b) == targets.end()) targets.push_back(b);
        }
        freeifaddrs(ifs);
    }
#endif
    for (uint32_t t : targets) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(t);
        a.sin_port = htons(DEFAULT_PORT);
        sendRaw(lan.disc, a, w.b);
    }
}

void leave() {
    lanLeave();
    steamLeave();
    g_packets.clear();
}

const std::vector<LobbyInfo>& lobbies() { return g_lobbies; }

void setStatus(const std::string& status, int members) {
    g_status = status;
    g_statusMembers = members;
    steamSetLobbyStatus(status, members);
}

bool active() { return lan.hosting || lan.client || st.hosting || st.client; }
bool isHost() { return lan.hosting || st.hosting; }
bool connecting() { return (lan.client && !lan.connected) || (st.client && !st.connected); }

Backend backend() {
    if (lan.hosting || lan.client) return Backend::Lan;
    if (st.hosting || st.client) return Backend::Steam;
    return Backend::None;
}

PeerId selfId() {
    if (lan.hosting || lan.client) return lan.self;
    if (st.ready) return st.self;
    return 0;
}

PeerId hostId() {
    if (lan.hosting) return 1;
    if (lan.client) return lan.connected ? 1 : 0;
    if (st.hosting) return st.self;
    if (st.client) return st.connected ? st.hostId : 0;
    return 0;
}

std::vector<PeerId> peers() {
    std::vector<PeerId> out;
    if (lan.hosting || lan.connected) {
        for (auto& p : lan.peers) out.push_back(p.id);
    } else if (st.hosting) {
        out = st.peers;
    } else if (st.client && st.connected) {
        out.push_back(st.hostId);
    }
    return out;
}

std::string peerName(PeerId id) {
    if (backend() == Backend::Steam && st.ready) return cleanName(st.GetFriendPersonaName(st.fr, id));
    return "";
}

int pingMs(PeerId id) {
    if (backend() == Backend::Lan) {
        LanPeer* p = lanFindById(id);
        return p ? p->rtt : -1;
    }
    if (backend() == Backend::Steam && st.ready && st.GetSessionConnectionInfo) {
        SteamNetIdentity ident = makeIdentity(id);
        alignas(8) uint8_t status[2048] = {0}; // SteamNetConnectionRealTimeStatus_t: {int state; int ping; ...}
        int state = st.GetSessionConnectionInfo(st.nm, &ident, nullptr, status);
        if (state <= 0) return -1;
        int ping;
        std::memcpy(&ping, status + 4, sizeof ping);
        return ping >= 0 ? ping : -1;
    }
    return -1;
}

void send(PeerId to, const void* data, size_t len, bool reliable) {
    if (lan.hosting || lan.connected) {
        if (LanPeer* p = lanFindById(to)) lanSendTo(*p, data, len, reliable);
    } else if (st.hosting || (st.client && st.connected)) {
        steamSend(to, data, len, reliable);
    }
}

void broadcast(const void* data, size_t len, bool reliable) {
    if (lan.hosting) {
        for (auto& p : lan.peers) lanSendTo(p, data, len, reliable);
    } else if (st.hosting) {
        for (PeerId p : st.peers) steamSend(p, data, len, reliable);
    }
}

bool recv(Packet& out) {
    if (g_packets.empty()) return false;
    out = std::move(g_packets.front());
    g_packets.pop_front();
    return true;
}

} // namespace net
