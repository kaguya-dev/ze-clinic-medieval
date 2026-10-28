// Co-op: sessions, lobby, host snapshots and client-side prediction/smoothing.
// The host runs the whole simulation; clients send input and replay snapshots + cosmetic events.
#include "Defs.hpp"
#include "Net.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>

using namespace util;

namespace {
constexpr float SNAP_RATE = 1.f / 30.f, INPUT_RATE = 1.f / 30.f;
float angLerp(float a, float b, float t) { return a + std::remainder(b - a, 2 * PI) * t; }
} // namespace

int Game::slotForPeer(uint64_t peer) const {
    for (int i = 0; i < MAX_SLOTS; i++)
        if (players[i].active && players[i].peer == peer) return i;
    return -1;
}

// ============================================================ sessions

void Game::hostSession(bool steam, bool friendsOnly) {
    if (steam) {
        if (!net::steamReady()) {
            netMsg = net::steamError();
            netMsgT = 5;
            return;
        }
        if (!net::hostSteam(MAX_SLOTS, friendsOnly)) {
            netMsg = "COULD NOT CREATE STEAM LOBBY";
            netMsgT = 5;
            return;
        }
    } else if (!net::hostLan(net::DEFAULT_PORT)) {
        netMsg = "COULD NOT OPEN UDP PORT " + std::to_string(net::DEFAULT_PORT);
        netMsgT = 5;
        return;
    }
    role = Role::Host;
    mode = Mode::Lobby;
    paused = false;
    localSlot = 0;
    for (int i = 0; i < MAX_SLOTS; i++) players[i] = Player{};
    Player& p = players[0];
    p.active = true;
    p.name = net::localName();
    p.skin = save.skin;
    p.peer = net::selfId();
    resetPlayer(p, 0);
    lobbyStage = 0;
    welcomed = true;
    clearWorld();
    evOut.clear();
    net::setStatus("IN LOBBY", 1);
    setScreen(Screen::Main);
    audio::play(SFX_JOIN, 0.6f);
}

void Game::joinSession(bool steam, uint64_t lobby, const std::string& addr) {
    if (steam) {
        if (!net::steamReady()) {
            netMsg = net::steamError();
            netMsgT = 5;
            return;
        }
        net::joinSteam(lobby);
    } else {
        net::joinLan(addr);
        save.lastIp = addr;
        persist();
    }
    role = Role::Client;
    mode = Mode::Lobby;
    paused = false;
    welcomed = false;
    localSlot = 0;
    for (int i = 0; i < MAX_SLOTS; i++) players[i] = Player{};
    players[0].name = net::localName();
    players[0].skin = save.skin;
    clearWorld();
    netMsg = "JOINING...";
    netMsgT = 30;
}

void Game::leaveSession(const std::string& msg) {
    net::leave();
    role = Role::Offline;
    welcomed = false;
    evOut.clear();
    toTitle(msg);
}

void Game::playerJoined(uint64_t peer, const std::string& name, int skin) {
    int slot = -1;
    for (int i = 1; i < MAX_SLOTS && slot < 0; i++)
        if (!players[i].active) slot = i;
    W w;
    if (slot < 0) {
        w.u8(MSG_REJECT);
        w.str("LOBBY FULL");
        net::send(peer, w.b.data(), w.size(), true);
        return;
    }
    Player& p = players[slot];
    p = Player{};
    p.active = true;
    p.peer = peer;
    p.name = name.empty() ? "DOCTOR " + std::to_string(slot + 1) : name.substr(0, 12);
    p.skin = std::clamp(skin, 0, SKIN_COUNT - 1);
    resetPlayer(p, slot);
    if (mode == Mode::Play || mode == Mode::Shop) {
        // Late arrivals get a purse that roughly matches how far the run has come.
        p.coins = STAGE_START_COINS[startStage] + wave * 40;
        p.invulnT = 3;
    }
    w.u8(MSG_WELCOME);
    w.u8(slot);
    w.u8(uint32_t(mode));
    w.u8(runId);
    net::send(peer, w.b.data(), w.size(), true);
    sendLobby();
    fxToast(p.name + " JOINED THE CLINIC", SLOT_COL[slot]);
    sfx(SFX_JOIN, 0.7f);
}

void Game::playerLeft(int slot) {
    if (slot <= 0 || slot >= MAX_SLOTS || !players[slot].active) return;
    fxToast(players[slot].name + " LEFT", 0xc8a0a0);
    players[slot].active = false;
    players[slot].peer = 0;
    for (auto& e : enemies)
        if (e.tgt == slot) e.tgt = pickTarget(e.x, e.y);
    sendLobby();
}

void Game::hostStart() {
    runId++;
    W w;
    w.u8(MSG_START);
    w.u8(lobbyStage);
    w.u8(runId);
    net::broadcast(w.b.data(), w.size(), true);
    startRun(lobbyStage);
}

void Game::hostToLobby() {
    W w;
    w.u8(MSG_TO_LOBBY);
    net::broadcast(w.b.data(), w.size(), true);
    mode = Mode::Lobby;
    paused = false;
    clearWorld();
    for (auto& p : players) p.ready = false;
    syncStageAudio();
    sendLobby();
    net::setStatus("IN LOBBY", activeCount());
}

// ============================================================ messages

void Game::sendLobby() {
    if (role != Role::Host) return;
    W w;
    w.u8(MSG_LOBBY);
    w.u8(lobbyStage);
    w.u8(uint32_t(mode));
    for (int i = 0; i < MAX_SLOTS; i++) {
        const Player& p = players[i];
        w.u8(p.active);
        if (!p.active) continue;
        w.str(p.name);
        w.u8(p.skin);
        w.u8(p.ready);
        w.u16(uint32_t(std::clamp(p.ping, 0, 9999)));
    }
    net::broadcast(w.b.data(), w.size(), true);
}

void Game::sendInput() {
    const Player& p = me();
    W w;
    w.u8(MSG_INPUT);
    w.f(p.in.aimX);
    w.f(p.in.aimY);
    w.f(p.x);
    w.f(p.y);
    bool posValid = !forcePos && (mode == Mode::Play || mode == Mode::Shop);
    w.u8((p.in.lmb ? 1 : 0) | (p.in.interact ? 2 : 0) | (posValid ? 4 : 0));
    w.u8(p.in.clicks);
    net::send(net::hostId(), w.b.data(), w.size(), false);
}

void Game::sendAction(int kind, float x, float y, int arg) {
    if (role != Role::Client || !welcomed) return;
    W w;
    w.u8(MSG_ACTION);
    w.u8(kind);
    w.f(x);
    w.f(y);
    w.u32(uint32_t(arg));
    net::send(net::hostId(), w.b.data(), w.size(), true);
    // Instant local feedback for choices the host will confirm in the next snapshot.
    if (kind == ACT_SWITCH && arg >= 0 && arg < WEAPON_COUNT && me().owned[arg] && me().cur != arg) {
        me().cur = arg;
        audio::play(SFX_CLICK, 0.5f);
    }
    if (kind == ACT_READY) me().ready = arg != 0;
    if (kind == ACT_SKIN) me().skin = arg;
}

void Game::sendChat(const std::string& s) {
    if (role == Role::Offline) {
        fxChat(localSlot, s);
        return;
    }
    W w;
    w.u8(MSG_CHAT);
    if (role == Role::Host) {
        w.u8(localSlot);
        w.str(s);
        net::broadcast(w.b.data(), w.size(), true);
        fxChat(localSlot, s);
    } else {
        w.u8(0);
        w.str(s);
        net::send(net::hostId(), w.b.data(), w.size(), true);
    }
}

void Game::sendSnapshot() {
    W w;
    w.u8(MSG_SNAP);
    w.u8(runId);
    w.u32(++snapTick);
    w.u8(uint32_t(mode));
    w.u8(stage);
    w.u16(wave);
    w.u8(loop);
    w.u16(waveKilled);
    w.u16(waveTotal);
    w.f(barrHp);
    w.f(barrMax);
    w.u8(barrTier);
    w.u8((raining ? 1 : 0) | (dying ? 2 : 0) | (lastStand ? 4 : 0));
    w.f(waveT);
    w.u8(bossesThisRun);
    w.f(deathT);
    for (int i = 0; i < MAX_SLOTS; i++) {
        const Player& p = players[i];
        w.u8(p.active);
        if (!p.active) continue;
        w.i16(p.x * 4);
        w.i16(p.y * 4);
        w.i16(std::remainder(p.aimAng, 2 * PI) * 1000);
        w.f(p.hp);
        w.f(p.maxHp);
        w.u8(p.cur);
        uint32_t owned = 0;
        for (int k = 0; k < WEAPON_COUNT; k++) owned |= uint32_t(p.owned[k]) << k;
        w.u16(owned);
        for (int k = 0; k < WEAPON_COUNT; k++) w.u8(uint32_t(std::clamp(p.ammo[k], 0, 255)));
        w.u8((p.reloading ? 1 : 0) | (p.jammed ? 2 : 0) | (p.down ? 4 : 0) | (p.dead ? 8 : 0) | (p.ready ? 16 : 0));
        w.f(p.reloadT);
        w.f(p.reloadDur);
        w.u8(uint32_t(std::clamp(p.blessedShots, 0, 255)));
        for (int k = 0; k < 4; k++) w.u8(p.up[k]);
        w.u8(p.flasks);
        w.u8(p.traps);
        w.u8(p.tonics);
        w.u32(uint32_t(std::max(0, p.coins)));
        w.u16(p.kills);
        w.u16(p.combo);
        w.f(p.comboT);
        w.u16(p.bestCombo);
        w.f(p.downT);
        w.q8(p.reviveT, 250);
        w.u8(p.skin);
        w.u8(p.revives);
        w.u8(p.downs);
        w.f(p.dmgDealt);
        w.q8(std::max(0.f, p.invulnT), 50);
    }
    w.u16(uint32_t(enemies.size()));
    for (auto& e : enemies) {
        w.u16(e.id);
        w.u8(e.t);
        w.i16(e.x * 4);
        w.i16(e.y * 4);
        w.i16(e.z * 4);
        w.f(e.hp);
        w.f(e.maxHp);
        w.u8((e.burn > 0 ? 1 : 0) | (e.attacking ? 2 : 0) | (e.trapT > 0 ? 4 : 0) | (e.vx < 0 ? 8 : 0) | (e.over ? 16 : 0) |
             (e.gas > 0 ? 32 : 0) | (e.mark > 0 ? 64 : 0) | (e.stun > 0 ? 128 : 0));
        w.u8(e.state);
        w.q8(e.stT, 50);
        w.q8(e.lunge, 255);
    }
    w.u8(uint32_t(std::min<size_t>(globs.size(), 255)));
    for (size_t i = 0; i < globs.size() && i < 255; i++) {
        const Glob& g = globs[i];
        w.i16(g.x * 4);
        w.i16(g.y * 4);
        w.i16(g.vx * 4);
        w.i16(g.vy * 4);
        w.u8(g.kind);
    }
    w.u8(uint32_t(std::min<size_t>(trapList.size(), 255)));
    for (size_t i = 0; i < trapList.size() && i < 255; i++) {
        w.i16(trapList[i].x * 4);
        w.i16(trapList[i].y * 4);
        w.u8(trapList[i].shut);
        w.q8(trapList[i].t, 50);
    }
    w.u8(uint32_t(std::min<size_t>(fires.size(), 255)));
    for (size_t i = 0; i < fires.size() && i < 255; i++) {
        w.i16(fires[i].x * 4);
        w.i16(fires[i].y * 4);
        w.q8(fires[i].life, 50);
    }
    w.u8(uint32_t(std::min<size_t>(gases.size(), 255)));
    for (size_t i = 0; i < gases.size() && i < 255; i++) {
        w.i16(gases[i].x * 4);
        w.i16(gases[i].y * 4);
        w.u8(uint32_t(gases[i].r));
        w.q8(gases[i].life, 40);
    }
    w.u8(uint32_t(std::min<size_t>(hazards.size(), 255)));
    for (size_t i = 0; i < hazards.size() && i < 255; i++) {
        const Hazard& h = hazards[i];
        w.i16(h.x * 4);
        w.i16(h.y * 4);
        w.u8(uint32_t(h.r));
        w.q8(h.t, 100);
        w.q8(h.dur, 100);
        w.u8(h.done);
    }
    net::broadcast(w.b.data(), w.size(), false);
}

void Game::applySnapshot(Rd& r) {
    uint8_t rid = uint8_t(r.u8());
    uint32_t tick = r.u32();
    if (!r.ok()) return;
    if (mode == Mode::Lobby || mode == Mode::Title) {
        // Only a snapshot from a newer run pulls us out of the lobby (the START message may still be in flight).
        if (uint8_t(rid - runId) == 0 || uint8_t(rid - runId) > 128) return;
    } else if (rid != runId) {
        if (uint8_t(rid - runId) > 128) return;
    }
    if (rid == runId && tick <= lastSnapTick && mode != Mode::Lobby) return;
    if (rid != runId || mode == Mode::Lobby) {
        runId = rid;
        clearWorld();
        dying = false;
        bossesThisRun = 0;
        countedKills = countedBosses = countedRevives = 0;
        save.runs++;
        save.coopRuns++;
        forcePos = true;
    }
    lastSnapTick = tick;
    snapAge = 0;
    Mode m = Mode(std::min<uint32_t>(r.u8(), uint32_t(Mode::GameOver)));
    int st = r.u8() % STAGE_COUNT;
    int wv = r.u16();
    loop = r.u8();
    waveKilled = r.u16();
    waveTotal = r.u16();
    barrHp = r.f();
    barrMax = std::max(1.f, r.f());
    barrTier = std::min<uint32_t>(r.u8(), 2);
    uint32_t fl = r.u8();
    bool rain = fl & 1;
    dying = fl & 2;
    lastStand = fl & 4;
    waveT = r.f();
    bossesThisRun = r.u8();
    deathT = r.f();
    if (st != stage || rain != raining || shownStage < 0) {
        bool changed = st != stage;
        stage = st;
        raining = rain;
        if (changed) {
            weather.clear();
            decalClear = true;
        }
        syncStageAudio();
    }
    if (wv != wave) {
        wave = wv;
        if (wave > save.bestWave) {
            save.bestWave = wave;
            newBest = true;
            checkUnlocks(true);
        }
    }
    if (m != mode) {
        Mode old = mode;
        mode = m;
        paused = paused && m != Mode::GameOver;
        if (m == Mode::Shop || old == Mode::Shop) {
            pickups.clear();
            flaskList.clear();
            globs.clear();
        }
        if (m == Mode::GameOver) {
            save.bestCombo = std::max(save.bestCombo, me().bestCombo);
            persist();
            checkUnlocks(true);
        }
        syncStageAudio();
    }

    for (int i = 0; i < MAX_SLOTS; i++) {
        Player& p = players[i];
        bool act = r.u8();
        if (!r.ok()) return;
        if (!act) {
            p.active = false;
            continue;
        }
        bool fresh = !p.active;
        p.active = true;
        float x = r.i16() / 4, y = r.i16() / 4, aim = r.i16() / 1000;
        p.hp = r.f();
        p.maxHp = std::max(1.f, r.f());
        int cur = r.u8() % WEAPON_COUNT;
        uint32_t owned = r.u16();
        for (int k = 0; k < WEAPON_COUNT; k++) p.owned[k] = owned & (1u << k);
        for (int k = 0; k < WEAPON_COUNT; k++) p.ammo[k] = r.u8();
        uint32_t pf = r.u8();
        p.reloading = pf & 1;
        p.jammed = pf & 2;
        bool wasDown = p.down;
        p.down = pf & 4;
        p.dead = pf & 8;
        bool ready = pf & 16;
        p.reloadT = r.f();
        p.reloadDur = std::max(0.1f, r.f());
        p.blessedShots = r.u8();
        for (int k = 0; k < 4; k++) p.up[k] = r.u8();
        p.flasks = r.u8();
        p.traps = r.u8();
        p.tonics = r.u8();
        p.coins = int(r.u32());
        p.kills = r.u16();
        p.combo = r.u16();
        p.comboT = r.f();
        p.bestCombo = r.u16();
        p.downT = r.f();
        p.reviveT = r.q8(250);
        int skin = r.u8() % SKIN_COUNT;
        p.revives = r.u8();
        p.downs = r.u8();
        p.dmgDealt = r.f();
        p.invulnT = r.q8(50);
        p.skin = skin;
        p.nx = x;
        p.ny = y;
        p.nAim = aim;
        if (i == localSlot) {
            // Our own position, aim and weapon choice are predicted locally; accept the host's only when
            // we can't move or have drifted far apart.
            if (forcePos || fresh || p.down || p.dead || wasDown != p.down || std::hypot(p.x - x, p.y - y) > 24) {
                p.x = x;
                p.y = y;
            }
            if (mode != Mode::Shop || fresh) p.ready = ready;
            if (p.owned[cur] && (fresh || mode == Mode::Shop)) p.cur = cur;
            else if (!p.owned[p.cur]) p.cur = cur;
        } else {
            p.ready = ready;
            p.cur = cur;
            if (fresh || std::hypot(p.x - x, p.y - y) > 40) {
                p.x = x;
                p.y = y;
                p.aimAng = aim;
            }
        }
    }

    forcePos = false;

    // Enemies: update by id, add new ones, drop the ones the host no longer has.
    int n = r.u16();
    std::vector<Enemy> next;
    next.reserve(n);
    for (int k = 0; k < n && r.ok(); k++) {
        int id = r.u16();
        EType t = EType(std::min<uint32_t>(r.u8(), E_COUNT - 1));
        float x = r.i16() / 4, y = r.i16() / 4, z = r.i16() / 4;
        float hp = r.f(), mhp = std::max(1.f, r.f());
        uint32_t ef = r.u8();
        int state = r.u8();
        float stT = r.q8(50), lunge = r.q8(255);
        Enemy* old = nullptr;
        for (auto& e : enemies)
            if (e.id == id) old = &e;
        if (old && old->dead) continue; // killed by an event; the snapshot hasn't caught up
        Enemy e = old ? *old : Enemy{};
        if (!old) {
            e.id = id;
            e.x = x;
            e.y = y;
            e.z = z;
            e.anim = rnd(0, 10);
        }
        e.t = t;
        e.nx = x;
        e.ny = y;
        e.nz = z;
        e.hp = hp;
        e.maxHp = mhp;
        e.burn = (ef & 1) ? 1.f : 0.f;
        e.attacking = ef & 2;
        e.trapT = (ef & 4) ? 1.f : 0.f;
        e.vx = (ef & 8) ? -1.f : 1.f;
        e.over = ef & 16;
        e.gas = (ef & 32) ? 1.f : 0.f;
        e.mark = (ef & 64) ? 1.f : 0.f;
        e.stun = (ef & 128) ? 1.f : 0.f;
        e.state = state;
        e.stT = stT;
        e.lunge = std::max(e.lunge, lunge);
        next.push_back(e);
    }
    if (!r.ok()) return;
    enemies.swap(next);

    globs.clear();
    n = r.u8();
    for (int k = 0; k < n; k++) {
        Glob g{};
        g.x = r.i16() / 4;
        g.y = r.i16() / 4;
        g.vx = r.i16() / 4;
        g.vy = r.i16() / 4;
        g.kind = r.u8() % 3;
        globs.push_back(g);
    }
    trapList.clear();
    n = r.u8();
    for (int k = 0; k < n; k++) {
        Trap t{};
        t.x = r.i16() / 4;
        t.y = r.i16() / 4;
        t.shut = r.u8();
        t.t = r.q8(50);
        trapList.push_back(t);
    }
    fires.clear();
    n = r.u8();
    for (int k = 0; k < n; k++) {
        FirePatch f{};
        f.x = r.i16() / 4;
        f.y = r.i16() / 4;
        f.life = r.q8(50);
        fires.push_back(f);
    }
    gases.clear();
    n = r.u8();
    for (int k = 0; k < n; k++) {
        GasCloud g{};
        g.x = r.i16() / 4;
        g.y = r.i16() / 4;
        g.r = float(r.u8());
        g.life = r.q8(40);
        g.owner = -1;
        gases.push_back(g);
    }
    hazards.clear();
    n = r.u8();
    for (int k = 0; k < n; k++) {
        Hazard h{};
        h.x = r.i16() / 4;
        h.y = r.i16() / 4;
        h.r = float(r.u8());
        h.t = r.q8(100);
        h.dur = r.q8(100);
        h.done = r.u8();
        hazards.push_back(h);
    }
}

void Game::handleHostMsg(uint64_t from, Rd& r) {
    uint32_t type = r.u8();
    int slot = slotForPeer(from);
    switch (type) {
    case MSG_HELLO: {
        std::string ver = r.str(), name = r.str();
        int skin = r.u8();
        if (!r.ok()) return;
        if (ver != PROTO_VERSION) {
            W w;
            w.u8(MSG_REJECT);
            w.str("GAME VERSION MISMATCH");
            net::send(from, w.b.data(), w.size(), true);
            return;
        }
        if (slot >= 0) return; // duplicate hello
        if (name.empty()) name = net::peerName(from);
        playerJoined(from, name, skin);
        break;
    }
    case MSG_INPUT: {
        if (slot < 0) return;
        Player& p = players[slot];
        float ax = r.f(), ay = r.f(), x = r.f(), y = r.f();
        uint32_t fl = r.u8();
        uint8_t clicks = uint8_t(r.u8());
        if (!r.ok()) return;
        p.in.aimX = ax;
        p.in.aimY = ay;
        p.in.lmb = fl & 1;
        p.in.interact = fl & 2;
        p.in.clicks = clicks;
        if ((fl & 4) && p.alive() && (mode == Mode::Play || mode == Mode::Shop)) {
            float nx = clampf(x, YARD_X0, YARD_X1), ny = clampf(y, YARD_Y0, YARD_Y1);
            if (std::fabs(nx - p.x) > 0.3f || std::fabs(ny - p.y) > 0.3f) p.walk += 0.15f;
            p.x = nx;
            p.y = ny;
        }
        break;
    }
    case MSG_ACTION: {
        if (slot < 0) return;
        int kind = r.u8();
        float x = r.f(), y = r.f();
        int arg = int(r.u32());
        if (r.ok()) action(slot, kind, x, y, arg);
        break;
    }
    case MSG_CHAT: {
        if (slot < 0) return;
        r.u8();
        std::string s = r.str().substr(0, 60);
        if (!r.ok() || s.empty()) return;
        W w;
        w.u8(MSG_CHAT);
        w.u8(slot);
        w.str(s);
        net::broadcast(w.b.data(), w.size(), true);
        fxChat(slot, s);
        break;
    }
    default: break;
    }
}

void Game::handleClientMsg(Rd& r) {
    uint32_t type = r.u8();
    switch (type) {
    case MSG_WELCOME: {
        int slot = r.u8() % MAX_SLOTS;
        Mode m = Mode(std::min<uint32_t>(r.u8(), uint32_t(Mode::GameOver)));
        uint8_t rid = uint8_t(r.u8());
        if (!r.ok()) return;
        localSlot = slot;
        welcomed = true;
        netMsg.clear();
        Player& p = players[slot];
        p.active = true;
        p.name = net::localName();
        p.skin = save.skin;
        if (m == Mode::Play || m == Mode::Shop || m == Mode::GameOver) {
            // Joining a run in progress: the next snapshot fills everything in.
            runId = uint8_t(rid - 1);
            lastSnapTick = 0;
        } else {
            runId = rid;
        }
        audio::play(SFX_JOIN, 0.7f);
        break;
    }
    case MSG_REJECT: leaveSession("REJECTED: " + r.str()); break;
    case MSG_LOBBY: {
        lobbyStage = r.u8() % STAGE_COUNT;
        r.u8();
        for (int i = 0; i < MAX_SLOTS; i++) {
            Player& p = players[i];
            bool act = r.u8();
            if (!r.ok()) return;
            if (!act) {
                p.active = false;
                continue;
            }
            if (!p.active) resetPlayer(p, i);
            p.active = true;
            p.name = r.str();
            int skin = r.u8() % SKIN_COUNT;
            bool ready = r.u8();
            p.ping = int(r.u16());
            // Keep our own pending choices until the host has echoed them.
            if (i != localSlot || mode != Mode::Lobby) {
                p.skin = skin;
                p.ready = ready;
            } else if (skin == p.skin) {
                p.ready = ready;
            }
        }
        break;
    }
    case MSG_START: {
        r.u8();
        uint8_t rid = uint8_t(r.u8());
        if (!r.ok() || rid == runId) return;
        runId = rid;
        lastSnapTick = 0;
        clearWorld();
        mode = Mode::Play;
        paused = dying = false;
        bossesThisRun = 0;
        countedKills = countedBosses = countedRevives = 0;
        save.runs++;
        save.coopRuns++;
        forcePos = true;
        syncStageAudio();
        break;
    }
    case MSG_SNAP: applySnapshot(r); break;
    case MSG_EVENTS:
        if (mode == Mode::Play || mode == Mode::Shop || mode == Mode::GameOver) applyEvents(r);
        break;
    case MSG_CHAT: {
        int slot = r.u8() % MAX_SLOTS;
        std::string s = r.str();
        if (r.ok()) fxChat(slot, s);
        break;
    }
    case MSG_TO_LOBBY:
        mode = Mode::Lobby;
        paused = false;
        clearWorld();
        for (auto& p : players) p.ready = false;
        syncStageAudio();
        break;
    default: break;
    }
}

// ============================================================ per-frame

void Game::netEvents() {
    net::Event ev;
    while (net::pollEvent(ev)) {
        switch (ev.type) {
        case net::EventType::Connected: {
            if (role != Role::Client) break;
            W w;
            w.u8(MSG_HELLO);
            w.str(PROTO_VERSION);
            w.str(net::localName());
            w.u8(save.skin);
            net::send(net::hostId(), w.b.data(), w.size(), true);
            netMsg = "CONNECTED. SAYING HELLO...";
            netMsgT = 10;
            break;
        }
        case net::EventType::ConnectFailed:
            if (role == Role::Client) leaveSession("COULD NOT JOIN: " + ev.msg);
            break;
        case net::EventType::Disconnected:
            if (role != Role::Offline) leaveSession((role == Role::Host ? "SESSION CLOSED: " : "DISCONNECTED: ") + ev.msg);
            break;
        case net::EventType::PeerJoined: break; // wait for their HELLO
        case net::EventType::PeerLeft:
            if (role == Role::Host) playerLeft(slotForPeer(ev.peer));
            break;
        case net::EventType::LobbyList: break;
        case net::EventType::JoinRequested:
            if (online()) leaveSession("");
            joinSession(true, ev.lobby, "");
            break;
        }
    }
}

void Game::netReceive() {
    net::Packet pkt;
    while (net::recv(pkt)) {
        if (pkt.data.empty()) continue;
        Rd r(pkt.data);
        if (role == Role::Host) handleHostMsg(pkt.from, r);
        else if (role == Role::Client && pkt.from == net::hostId()) handleClientMsg(r);
        if (role == Role::Offline) break; // a message made us leave
    }
}

void Game::netUpdate(float dt) {
    net::update();
    netEvents();
    netReceive();
    if (role == Role::Host) {
        bool anyone = !net::peers().empty();
        netSendT -= dt;
        if (netSendT <= 0 || evOut.size() > 40000) {
            netSendT = SNAP_RATE;
            if (anyone && (mode == Mode::Play || mode == Mode::Shop || mode == Mode::GameOver)) sendSnapshot();
            if (anyone && evOut.size() > 0) {
                W w;
                w.u8(MSG_EVENTS);
                w.b.insert(w.b.end(), evOut.b.begin(), evOut.b.end());
                net::broadcast(w.b.data(), w.size(), true);
            }
            evOut.clear();
        }
        lobbySendT -= dt;
        if (lobbySendT <= 0) {
            lobbySendT = 1;
            for (auto& p : players)
                if (p.active && p.peer != net::selfId()) p.ping = net::pingMs(p.peer);
            if (mode == Mode::Lobby) sendLobby();
            std::string st = mode == Mode::Lobby ? "IN LOBBY" : (mode == Mode::GameOver ? "GAME OVER" : "WAVE " + std::to_string(wave));
            net::setStatus(st, activeCount());
        }
    } else if (role == Role::Client && welcomed) {
        inputSendT -= dt;
        snapAge += dt;
        if (inputSendT <= 0) {
            inputSendT = INPUT_RATE;
            sendInput();
        }
    } else {
        evOut.clear();
    }
}

// Client frame: move our own doctor, predict our shots, smooth everyone else.
void Game::updateClient(float dt) {
    float k = 1 - std::exp(-dt * 14);
    for (int i = 0; i < MAX_SLOTS; i++) {
        Player& p = players[i];
        if (!p.active) continue;
        p.flameSfxT -= dt;
        p.recoil = std::max(0.f, p.recoil - dt * 40);
        p.muzzleT -= dt;
        p.crossSpread = std::max(0.f, p.crossSpread - dt * 25);
        if (i == localSlot) continue;
        float ox = p.x, oy = p.y;
        p.x += (p.nx - p.x) * k;
        p.y += (p.ny - p.y) * k;
        if (std::hypot(p.x - ox, p.y - oy) > dt * 5) p.walk += dt * 9;
        p.aimAng = angLerp(p.aimAng, p.nAim, k);
        p.faceLeft = std::cos(p.aimAng) < 0;
    }

    Player& m = me();
    if (m.alive() && !dying) {
        if (m.in.moveX != 0 || m.in.moveY != 0) {
            float len = std::hypot(m.in.moveX, m.in.moveY);
            m.x = clampf(m.x + m.in.moveX / len * 62 * dt, YARD_X0, YARD_X1);
            m.y = clampf(m.y + m.in.moveY / len * 62 * 0.7f * dt, YARD_Y0, YARD_Y1);
            m.walk += dt * 9;
        }
        if (mode == Mode::Play) {
            float sx, sy;
            shoulder(m, sx, sy);
            float dx = m.in.aimX - sx, dy = m.in.aimY - sy;
            if (dx * dx + dy * dy > 9) m.aimAng = std::atan2(dy, dx);
            m.faceLeft = std::cos(m.aimAng) < 0;
            if (m.in.clicks != m.lastClicks) {
                m.lastClicks = m.in.clicks;
                m.fireQueue = 0.2f;
            }
            m.fireQueue -= dt;
            predCD -= dt * rateMul(m);
            const WeaponDef& d = WEAPONS[m.cur];
            bool want = d.hold ? m.in.lmb : m.fireQueue > 0;
            if (want && !m.reloading && predCD <= 0 && m.ammo[m.cur] > 0) {
                float x, y;
                muzzle(m, x, y);
                fxShot(localSlot, x, y, m.aimAng, m.cur, m.blessedShots > 0, xr());
                predCD = d.interval;
                m.fireQueue = 0;
                m.ammo[m.cur]--;
            }
        }
    }

    for (auto& e : enemies) {
        float ox = e.x;
        e.x += (e.nx - e.x) * k;
        e.y += (e.ny - e.y) * k;
        e.z += (e.nz - e.z) * k;
        const EnemyDef& d = EDEF[e.t];
        if (d.fly) e.anim += dt * (e.t == E_BAT ? 18 : (e.t == E_BISHOP ? 3 : 12));
        else if (std::fabs(e.x - ox) > dt * 2) e.anim += dt * (std::fabs(e.x - ox) / dt * 0.13f + 2);
        e.flash -= dt;
        e.lunge = std::max(0.f, e.lunge - dt * 5);
    }
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.dead; }), enemies.end());

    for (auto& g : globs) {
        if (g.kind != 1) g.vy += 200 * dt;
        g.x += g.vx * dt;
        g.y += g.vy * dt;
    }
    updateBullets(dt);
    for (auto& f : flaskList) f.t += dt;
    flaskList.erase(std::remove_if(flaskList.begin(), flaskList.end(), [](const Flask& f) { return f.t >= f.dur; }), flaskList.end());
    for (auto& h : hazards) h.t += dt;
    for (auto& f : fires) f.life -= dt;
    for (auto& g : gases) g.life -= dt;

    static bool bossMusic = false;
    bool boss = false;
    for (auto& e : enemies) boss |= isBoss(e.t);
    if (boss != bossMusic) {
        bossMusic = boss;
        syncStageAudio();
    }
}
