// Cosmetic effects. Functions named fx* are replicated: the host runs them and also records them in
// evOut, which is broadcast to clients and replayed through applyEvents().
#include "Defs.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>

using namespace util;

namespace {
enum Ev : uint8_t {
    EV_SFX = 1, EV_POPUP, EV_DAMAGE, EV_BANNER, EV_TOAST, EV_SHAKE, EV_SHOT, EV_ARC, EV_EXPLODE, EV_HIT, EV_KILL, EV_COINS,
    EV_FLASK, EV_BARR, EV_HURT, EV_RELOAD, EV_HEAL, EV_TRAP, EV_GLOB, EV_PING, EV_DOWN, EV_REVIVE, EV_THUNDER, EV_BEAM,
    EV_BURST, EV_PILLAR, EV_REPAIR, EV_CLANG,
};
uint32_t darker(uint32_t c) { return mixRGB(c, 0x000000, 0.35f); }
} // namespace

// ============================================================ replicated fx

void Game::sfx(int id, float vol, float pitch) {
    emit(EV_SFX, [&](W& w) { w.u8(id); w.f(vol); w.f(pitch); });
    audio::play(id, vol, pitch);
}

void Game::fxPopup(const std::string& s, float x, float y, uint32_t col, int scale, float life) {
    emit(EV_POPUP, [&](W& w) { w.str(s); w.f(x); w.f(y); w.u32(col); w.u8(scale); w.f(life); });
    popup(s, x, y, col, scale, life);
}

void Game::fxDamage(float x, float y, float dmg, bool crit) {
    emit(EV_DAMAGE, [&](W& w) { w.f(x); w.f(y); w.f(dmg); w.u8(crit); });
    if (!save.dmgNumbers) return;
    if (crit) popup(std::to_string(int(dmg)) + "!", x, y, 0xffe040, 1, 0.8f);
    else popup(std::to_string(int(std::max(1.f, dmg))), x, y, 0xf0f0f0, 1, 0.5f);
}

void Game::fxBanner(const std::string& t, const std::string& sub, uint32_t col, float dur) {
    emit(EV_BANNER, [&](W& w) { w.str(t); w.str(sub); w.u32(col); w.f(dur); });
    banner(t, sub, col, dur);
}

void Game::fxToast(const std::string& s, uint32_t col) {
    emit(EV_TOAST, [&](W& w) { w.str(s); w.u32(col); });
    toast(s, col);
}

void Game::fxShake(float s) {
    emit(EV_SHAKE, [&](W& w) { w.f(s); });
    addShake(s);
}

void Game::fxShot(int slot, float x, float y, float ang, int weapon, bool perfect, uint32_t seed) {
    emit(EV_SHOT, [&](W& w) { w.u8(slot); w.f(x); w.f(y); w.f(ang); w.u8(weapon); w.u8(perfect); w.u32(seed); });
    Player& p = players[slot];
    const WeaponDef& d = WEAPONS[weapon];
    Rng r(seed);
    bool local = slot == localSlot;
    if (d.kind == K_BULLET || d.kind == K_FLAME || d.kind == K_SAW || d.kind == K_LEECH) {
        float dmg = d.dmg * dmgMul(p) * (perfect ? 1.4f : 1.f);
        bool flame = d.kind == K_FLAME;
        for (int i = 0; i < d.pellets; i++) {
            float a = ang + (flame ? (r.f() - r.f()) * d.spread * 1.5f : r.f(-d.spread, d.spread));
            float sp = d.speed * (flame ? r.f(0.7f, 1.2f) : r.f(0.92f, 1.06f));
            Bullet b{};
            b.x = x;
            b.y = y;
            b.vx = std::cos(a) * sp;
            b.vy = std::sin(a) * sp;
            b.dmg = dmg;
            b.life = b.maxLife = flame ? r.f(0.35f, 0.5f) : (d.kind == K_SAW ? 2.4f : 1.4f);
            b.blast = d.blast;
            b.knock = d.knock;
            b.pierce = d.pierce;
            b.kind = d.kind;
            b.weapon = weapon;
            b.owner = slot;
            b.perfect = perfect;
            b.cosmetic = !authority();
            b.col = perfect ? 0xffe060 : d.col;
            float aimY = (authority() || local) ? p.in.aimY : y + 30;
            b.floorY = clampf(aimY, LANE_TOP + 2, LANE_BOT);
            if (d.kind == K_SAW) b.vy -= 60;
            bullets.push_back(b);
        }
    }
    p.muzzleT = d.kind == K_FLAME ? 0.03f : 0.07f;
    p.recoil = d.kind == K_FLAME ? 0.5f : std::min(4.f, d.knock * 0.04f);
    p.crossSpread = std::min(7.f, p.crossSpread + (d.kind == K_FLAME ? 0.4f : 2 + d.shake));
    if (local) addShake(d.shake);
    float vol = local ? 1.f : 0.55f;
    float c = std::cos(ang), s = std::sin(ang);
    if (d.kind == K_FLAME) {
        if (p.flameSfxT <= 0) {
            audio::play(SFX_FLAME, 0.4f * vol, rnd(0.85f, 1.1f));
            p.flameSfxT = 0.09f;
        }
        return;
    }
    if (d.kind == K_LIGHTNING) {
        for (int k = 0; k < 5; k++) addPart(x, y, rnd(-60, 60), rnd(-60, 60), rnd(0.1f, 0.2f), 0xa8e8ff, 3);
        audio::play(SFX_ZAP, 0.55f * vol, rnd(0.9f, 1.15f));
        tlights.push_back({x, y, 70, 0.1f, 0.1f, 0x8ad8ff});
        return;
    }
    for (int k = 0; k < 4; k++)
        addPart(x, y, c * rnd(10, 35) + rnd(-6, 6), s * rnd(10, 35) - rnd(4, 14), rnd(0.5f, 1.0f), d.kind == K_GAS ? 0x7a9a5a : 0x8a8a9a,
                1, rnd(3, 6), -12, 999, 2);
    if (d.kind != K_GAS)
        for (int k = 0; k < 6; k++) {
            float a = ang + rnd(-0.5f, 0.5f), sp = rnd(80, 180);
            addPart(x, y, std::cos(a) * sp, std::sin(a) * sp, rnd(0.08f, 0.18f), 0xffd070, 3);
        }
    if (weapon == 0 || weapon == 1 || weapon == 4 || weapon == 5) {
        float sx, sy;
        shoulder(p, sx, sy);
        addPart(sx, sy, -c * rnd(20, 40) + rnd(-10, 10), -rnd(60, 100), 1.6f, 0xd8b048, 5, 1, 420, p.y - rnd(0, 3));
    }
    audio::play(d.sfx, (perfect ? 0.85f : 0.7f) * vol, rnd(0.92f, 1.08f) * (perfect ? 0.9f : 1.f));
    tlights.push_back({x, y, weapon == 5 ? 120.f : 80.f, 0.08f, 0.08f, 0xffc070});
}

void Game::fxArc(const std::vector<std::pair<float, float>>& pts, uint32_t col) {
    emit(EV_ARC, [&](W& w) {
        w.u32(col);
        w.u8(uint32_t(pts.size()));
        for (auto& p : pts) { w.f(p.first); w.f(p.second); }
    });
    // Jagged subdivision so the bolt crackles.
    Arc a;
    a.col = col;
    a.life = 0.16f;
    for (size_t i = 0; i < pts.size(); i++) {
        if (i > 0) {
            float x0 = pts[i - 1].first, y0 = pts[i - 1].second, x1 = pts[i].first, y1 = pts[i].second;
            int n = std::max(2, int(std::hypot(x1 - x0, y1 - y0) / 7));
            for (int k = 1; k < n; k++) {
                float u = float(k) / n;
                a.pts.push_back({lerp(x0, x1, u) + rnd(-3, 3), lerp(y0, y1, u) + rnd(-3, 3)});
            }
        }
        a.pts.push_back(pts[i]);
        if (i > 0) {
            for (int k = 0; k < 4; k++) addPart(pts[i].first, pts[i].second, rnd(-70, 70), rnd(-70, 70), rnd(0.1f, 0.25f), 0xc0f0ff, 3);
            tlights.push_back({pts[i].first, pts[i].second, 40, 0.15f, 0.15f, col});
        }
    }
    arcs.push_back(a);
}

void Game::fxExplode(float x, float y, float r, bool big) {
    emit(EV_EXPLODE, [&](W& w) { w.f(x); w.f(y); w.f(r); w.u8(big); });
    for (int k = 0; k < int(r * 0.5f); k++)
        addPart(x + rnd(-r / 3, r / 3), y + rnd(-r / 4, r / 4), rnd(-70, 70), rnd(-110, -10), rnd(0.25f, 0.6f), 0xffa030, 4,
                rnd(r * 0.25f, r * 0.5f), -40, 999, 3);
    for (int k = 0; k < 24; k++) {
        float a = rnd(0, 2 * PI), sp = rnd(80, 260);
        addPart(x, y, std::cos(a) * sp, std::sin(a) * sp * 0.7f - 40, rnd(0.15f, 0.4f), 0xffe080, 3, 1, 200);
    }
    for (int k = 0; k < 10; k++)
        addPart(x + rnd(-r / 3, r / 3), y + rnd(-6, 6), rnd(-20, 20), rnd(-35, -10), rnd(1.2f, 2.4f), 0x4a4450, 1, rnd(6, 12), -6, 999, 1.2f);
    for (int k = 0; k < 10; k++)
        addPart(x, y, rnd(-90, 90), rnd(-160, -60), rnd(1.5f, 3), k % 2 ? 0x3a3646 : 0x5a5060, 2, 1, 420,
                clampf(y + rnd(-4, 8), LANE_TOP, LANE_BOT + 4));
    tlights.push_back({x, y, r * 2.6f, 0.4f, 0.4f, 0xffa040});
    decals.push_back({x, clampf(y + 4, LANE_TOP, LANE_BOT), r * 0.35f, 0x120e16});
    addShake(big ? 7.f : 3.5f);
    audio::play(SFX_EXPLODE, big ? 0.95f : 0.7f, rnd(0.9f, 1.1f) * (big ? 0.8f : 1.f));
}

void Game::fxHit(int id, float x, float y, float floorY, uint32_t col, bool crit, float dirX) {
    emit(EV_HIT, [&](W& w) { w.u16(id); w.f(x); w.f(y); w.f(floorY); w.u32(col); w.u8(crit); w.f(dirX); });
    blood(x, y, floorY, col, crit ? 8 : 4, dirX);
    if (crit)
        for (int k = 0; k < 5; k++) addPart(x, y, rnd(-80, 80), rnd(-120, -20), rnd(0.1f, 0.25f), 0xfff0a0, 3, 1, 200);
    if (!authority())
        for (auto& e : enemies)
            if (e.id == id) e.flash = 0.07f;
}

void Game::fxKill(const Enemy& e) {
    emit(EV_KILL, [&](W& w) { w.u16(e.id); w.u8(e.t); w.f(e.x); w.f(e.y); w.f(e.z); });
    gibs(e);
    switch (e.t) {
    case E_RAT:
    case E_PLAGUERAT: audio::play(SFX_SQUEAK, 0.5f, rnd(0.9f, 1.2f)); break;
    case E_RATLING: audio::play(SFX_SQUEAK, 0.35f, rnd(1.3f, 1.6f)); break;
    case E_GIANTRAT: audio::play(SFX_SQUEAK, 0.7f, 0.6f); break;
    case E_RATKING: audio::play(SFX_SQUEAK, 1.0f, 0.4f); break;
    case E_CROW: audio::play(SFX_CAW, 0.5f, 1.3f); break;
    case E_BAT: audio::play(SFX_SCREECH, 0.35f, 1.4f); break;
    case E_KNIGHT:
        audio::play(SFX_CLANG, 0.6f, 0.7f);
        audio::play(SFX_GROAN, 0.4f, 0.7f);
        break;
    case E_HORSEMAN:
        audio::play(SFX_NEIGH, 1.f, 0.7f);
        audio::play(SFX_EXPLODE, 0.6f, 0.6f);
        break;
    case E_BISHOP:
        audio::play(SFX_CHANT, 1.f, 0.6f);
        audio::play(SFX_PILLAR, 0.8f, 0.7f);
        break;
    default:
        audio::play(SFX_SPLAT, 0.7f, rnd(0.8f, 1.1f));
        audio::play(SFX_GROAN, 0.4f, rnd(0.6f, 0.8f));
        break;
    }
    if (!authority())
        for (auto& o : enemies)
            if (o.id == e.id) o.dead = true;
}

void Game::fxCoins(float x, float y, int val, int owner) {
    emit(EV_COINS, [&](W& w) { w.f(x); w.f(y); w.u32(val); w.u8(owner); });
    spawnCoins(x, y, val, owner);
}

void Game::fxFlask(float x0, float y0, float x1, float y1, float dur, int kind, int owner) {
    emit(EV_FLASK, [&](W& w) { w.f(x0); w.f(y0); w.f(x1); w.f(y1); w.f(dur); w.u8(kind); w.u8(owner); });
    flaskList.push_back({x0, y0, x1, y1, 0, dur, kind, owner, !authority()});
    if (kind == 0) audio::play(SFX_THROW, 0.6f);
}

void Game::fxBarricade(bool broken) {
    emit(EV_BARR, [&](W& w) { w.u8(broken); });
    uint32_t wood = barrTier == 2 ? 0x8a8f99 : 0x8a5a34;
    if (!broken) {
        addShake(0.8f);
        audio::play(SFX_THUD, 0.5f, rnd(0.8f, 1.2f));
        for (int k = 0; k < 5; k++) {
            float fy = rnd(LANE_TOP, LANE_BOT);
            addPart(BARR_X + rnd(-3, 3), fy - rnd(4, 18), rnd(-70, 20), rnd(-110, -30), rnd(1, 2), wood, 2, 1, 400, fy);
        }
        return;
    }
    audio::play(SFX_BREAK, 0.9f);
    addShake(6);
    for (int k = 0; k < 40; k++) {
        float fy = rnd(LANE_TOP, LANE_BOT);
        addPart(BARR_X + rnd(-4, 4), fy - rnd(0, 24), rnd(-120, 80), rnd(-180, -40), rnd(2, 4), k % 3 ? wood : 0x48291a, 2, rnd(1, 2), 420, fy);
    }
}

void Game::fxHurt(int slot, float d) {
    emit(EV_HURT, [&](W& w) { w.u8(slot); w.f(d); });
    Player& p = players[slot];
    bool local = slot == localSlot;
    p.hurtFlash = 0.4f;
    if (local) addShake(3);
    audio::play(SFX_HURT, local ? 0.7f : 0.4f, rnd(0.9f, 1.1f));
    popup("-" + std::to_string(int(std::ceil(d))), p.x, p.y - 36, local ? 0xff4040 : 0xc06060, 1, 0.9f);
    blood(p.x, p.y - 16, p.y, 0x8a1020, 8, 1);
}

void Game::fxReload(int slot, int kind) {
    emit(EV_RELOAD, [&](W& w) { w.u8(slot); w.u8(kind); });
    Player& p = players[slot];
    bool local = slot == localSlot;
    if (kind == 2) {
        audio::play(SFX_RELOAD, local ? 0.5f : 0.2f, 1.2f);
        return;
    }
    if (kind == 0) {
        float sx, sy;
        shoulder(p, sx, sy);
        popup("PERFECT!", p.x, p.y - 46, 0xffe060, 1, 1.0f);
        audio::play(SFX_PERFECT, local ? 0.7f : 0.3f);
        for (int k = 0; k < 16; k++) {
            float a = rnd(0, 2 * PI), sp = rnd(30, 90);
            addPart(sx, sy, std::cos(a) * sp, std::sin(a) * sp, rnd(0.3f, 0.6f), 0xffe060, 6, 1, 0, 999, 3);
        }
    } else {
        popup("JAMMED!", p.x, p.y - 46, 0xff5040, 1, 0.9f);
        audio::play(SFX_JAM, local ? 0.6f : 0.3f);
    }
}

void Game::fxHealPlayer(int slot, float amount, bool big) {
    emit(EV_HEAL, [&](W& w) { w.u8(slot); w.f(amount); w.u8(big); });
    Player& p = players[slot];
    if (!big) {
        if (chance(0.5f)) addPart(p.x + rnd(-6, 6), p.y - rnd(4, 24), rnd(-5, 5), rnd(-30, -15), rnd(0.4f, 0.7f), 0xff7a9a, 6);
        return;
    }
    popup("+" + std::to_string(int(amount)), p.x, p.y - 40, 0x60ff80, 1, 1.0f);
    audio::play(SFX_DRINK, 0.7f);
    for (int k = 0; k < 18; k++) addPart(p.x + rnd(-8, 8), p.y - rnd(0, 26), rnd(-8, 8), rnd(-50, -20), rnd(0.5f, 1.0f), 0x60ff80, 6);
}

void Game::fxTrap(float x, float y, bool snap) {
    emit(EV_TRAP, [&](W& w) { w.f(x); w.f(y); w.u8(snap); });
    if (snap) {
        audio::play(SFX_TRAP, 0.7f);
        for (int k = 0; k < 8; k++) addPart(x, y - 3, rnd(-90, 90), rnd(-120, -30), rnd(0.1f, 0.25f), 0xfff0a0, 3, 1, 200);
    } else {
        audio::play(SFX_TRAP, 0.3f, 1.6f);
        for (int k = 0; k < 6; k++) addPart(x + rnd(-6, 6), y, rnd(-20, 20), rnd(-40, -10), 0.4f, 0x8a8478, 0, 1, 200, y + 1);
    }
}

void Game::fxGlobPop(float x, float y, int kind, bool shot) {
    emit(EV_GLOB, [&](W& w) { w.f(x); w.f(y); w.u8(kind); w.u8(shot); });
    uint32_t col = kind == 2 ? 0xff8a30 : (kind == 1 ? 0xd8d0c0 : 0xa8ff3a);
    if (shot) {
        popup("+1", x, y - 6, 0xffd860);
        audio::play(SFX_SPLAT, 0.3f, 1.6f);
    }
    for (int k = 0; k < 8; k++) addPart(x, y, rnd(-60, 60), rnd(-60, 20), rnd(0.2f, 0.4f), col, kind == 1 ? 3 : 6, 1, 200);
    if (kind == 0 && !shot) decals.push_back({x, y, 2, 0x4a7a1a});
    if (kind == 2) tlights.push_back({x, y, 30, 0.2f, 0.2f, 0xff8030});
}

void Game::fxPing(int slot, float x, float y) {
    emit(EV_PING, [&](W& w) { w.u8(slot); w.f(x); w.f(y); });
    pings.push_back({x, y, 3.f, slot});
    audio::play(SFX_PING, 0.6f, 1.f + slot * 0.08f);
}

void Game::fxDown(int slot) {
    emit(EV_DOWN, [&](W& w) { w.u8(slot); });
    Player& p = players[slot];
    audio::play(SFX_DOWN, 0.8f);
    blood(p.x, p.y - 8, p.y, 0x8a1020, 14, 1);
    if (slot == localSlot) addShake(5);
}

void Game::fxRevive(int slot) {
    emit(EV_REVIVE, [&](W& w) { w.u8(slot); });
    Player& p = players[slot];
    audio::play(SFX_REVIVE, 0.8f);
    for (int k = 0; k < 30; k++) addPart(p.x + rnd(-8, 8), p.y - rnd(0, 6), rnd(-10, 10), rnd(-90, -30), rnd(0.6f, 1.2f), 0xfff0a0, 6);
    tlights.push_back({p.x, p.y - 14, 70, 0.8f, 0.8f, 0xfff0a0});
    popup("REVIVED!", p.x, p.y - 44, 0x8aff8a, 1, 1.2f);
}

void Game::fxThunder(uint32_t seed) {
    emit(EV_THUNDER, [&](W& w) { w.u32(seed); });
    Rng r(seed);
    flashT = 0.38f;
    thunderT = r.f(0.3f, 1.2f);
    bolt.clear();
    float x = r.f(20, 360), y = 0, end = r.f(100, 145);
    while (y < end) {
        bolt.push_back({x, y});
        x += r.f(-10, 10);
        y += r.f(6, 14);
    }
    bolt.push_back({x, y});
}

void Game::fxBeam(float x0, float y0, float x1, float y1, uint32_t col) {
    emit(EV_BEAM, [&](W& w) { w.f(x0); w.f(y0); w.f(x1); w.f(y1); w.u32(col); });
    Arc a;
    a.col = col;
    a.life = 0.35f;
    int n = std::max(2, int(std::hypot(x1 - x0, y1 - y0) / 6));
    for (int k = 0; k <= n; k++) {
        float u = float(k) / n;
        a.pts.push_back({lerp(x0, x1, u), lerp(y0, y1, u) - std::sin(PI * u) * 8 + rnd(-1, 1)});
    }
    arcs.push_back(a);
    for (int k = 0; k < 6; k++) addPart(x1 + rnd(-4, 4), y1 + rnd(-4, 4), rnd(-10, 10), rnd(-30, -10), rnd(0.4f, 0.8f), col, 6);
}

void Game::fxBurst(float x, float y, int n, uint32_t col, int kind, float spd, float life) {
    emit(EV_BURST, [&](W& w) { w.f(x); w.f(y); w.u8(n); w.u32(col); w.u8(kind); w.f(spd); w.f(life); });
    for (int k = 0; k < n; k++) {
        float a = rnd(0, 2 * PI), sp = rnd(spd * 0.3f, spd);
        if (kind == 1) addPart(x + rnd(-4, 4), y + rnd(-3, 3), std::cos(a) * sp * 0.5f, -rnd(5, spd * 0.6f), rnd(life * 0.6f, life), col, 1, rnd(3, 7), -8, 999, 2);
        else if (kind == 0 || kind == 2)
            addPart(x, y, std::cos(a) * sp, -std::fabs(std::sin(a)) * sp - 20, rnd(life * 0.6f, life), col, uint8_t(kind), 1, 300, y + rnd(0, 3));
        else addPart(x, y, std::cos(a) * sp, std::sin(a) * sp, rnd(life * 0.6f, life), col, uint8_t(kind), kind == 4 ? rnd(3, 6) : 1, kind == 4 ? -30 : 0);
    }
}

void Game::fxPillar(float x, float y, float r) {
    emit(EV_PILLAR, [&](W& w) { w.f(x); w.f(y); w.f(r); });
    for (int k = 0; k < 40; k++)
        addPart(x + rnd(-r * 0.6f, r * 0.6f), y - rnd(0, 6), rnd(-15, 15), rnd(-260, -120), rnd(0.3f, 0.7f), k % 3 ? 0xff7a20 : 0xffe080, 4,
                rnd(4, 9), -60, 999, 1);
    for (int k = 0; k < 16; k++) addPart(x, y - 4, rnd(-120, 120), rnd(-80, -10), rnd(0.15f, 0.35f), 0xffe080, 3, 1, 200);
    tlights.push_back({x, y - 30, 110, 0.5f, 0.5f, 0xff7030});
    decals.push_back({x, y, r * 0.4f, 0x120e16});
    addShake(4);
    audio::play(SFX_PILLAR, 0.8f, rnd(0.9f, 1.1f));
}

void Game::fxRepair(float x, float y) {
    emit(EV_REPAIR, [&](W& w) { w.f(x); w.f(y); });
    audio::play(SFX_REPAIR, 0.5f, rnd(0.9f, 1.15f));
    for (int k = 0; k < 4; k++) addPart(x + rnd(-3, 3), y - rnd(6, 18), rnd(-30, 30), rnd(-60, -20), rnd(0.4f, 0.8f), 0xc89a5a, 2, 1, 300, y);
    addPart(x, y - 20, 0, -20, 0.6f, 0x8aff8a, 6);
}

void Game::fxClang(float x, float y) {
    emit(EV_CLANG, [&](W& w) { w.f(x); w.f(y); });
    audio::play(SFX_CLANG, 0.45f, rnd(0.9f, 1.25f));
    for (int k = 0; k < 6; k++) addPart(x, y, rnd(20, 120), rnd(-90, 30), rnd(0.08f, 0.2f), 0xfff0c0, 3, 1, 200);
    if (chance(0.25f)) popup("BLOCKED", x, y - 8, 0xa0a8b8, 1, 0.5f);
}

void Game::fxChat(int slot, const std::string& s) {
    std::string who = slot >= 0 && slot < MAX_SLOTS ? players[slot].name : "";
    chats.push_back({who.empty() ? s : who + ": " + s, 9.f, slot >= 0 && slot < MAX_SLOTS ? SLOT_COL[slot] : 0xc8c0b0});
    if (chats.size() > 6) chats.erase(chats.begin());
    audio::play(SFX_HOVER, 0.5f, 1.4f);
}

// Replays a batch of host events on a client.
void Game::applyEvents(Rd& r) {
    while (r.more()) {
        uint32_t t = r.u8();
        switch (t) {
        case EV_SFX: { int id = r.u8(); float v = r.f(), p = r.f(); if (id < SFX_COUNT) audio::play(id, v, p); break; }
        case EV_POPUP: { std::string s = r.str(); float x = r.f(), y = r.f(); uint32_t c = r.u32(); int sc = r.u8(); float l = r.f(); popup(s, x, y, c, sc, l); break; }
        case EV_DAMAGE: { float x = r.f(), y = r.f(), d = r.f(); bool c = r.u8(); fxDamage(x, y, d, c); break; }
        case EV_BANNER: { std::string a = r.str(), b = r.str(); uint32_t c = r.u32(); float d = r.f(); banner(a, b, c, d); break; }
        case EV_TOAST: { std::string s = r.str(); uint32_t c = r.u32(); toast(s, c); break; }
        case EV_SHAKE: addShake(r.f()); break;
        case EV_SHOT: {
            int slot = r.u8() % MAX_SLOTS;
            float x = r.f(), y = r.f(), a = r.f();
            int w = r.u8() % WEAPON_COUNT;
            bool perfect = r.u8();
            uint32_t seed = r.u32();
            if (slot != localSlot) fxShot(slot, x, y, a, w, perfect, seed); // our own shots were predicted locally
            break;
        }
        case EV_ARC: {
            uint32_t c = r.u32();
            int n = r.u8();
            std::vector<std::pair<float, float>> pts;
            for (int i = 0; i < n; i++) { float x = r.f(); pts.push_back({x, r.f()}); }
            if (r.ok()) fxArc(pts, c);
            break;
        }
        case EV_EXPLODE: { float x = r.f(), y = r.f(), rr = r.f(); bool b = r.u8(); fxExplode(x, y, rr, b); break; }
        case EV_HIT: { int id = r.u16(); float x = r.f(), y = r.f(), fy = r.f(); uint32_t c = r.u32(); bool cr = r.u8(); float d = r.f(); fxHit(id, x, y, fy, c, cr, d); break; }
        case EV_KILL: {
            Enemy e;
            e.id = r.u16();
            e.t = EType(std::min<uint32_t>(r.u8(), E_COUNT - 1));
            e.x = r.f();
            e.y = r.f();
            e.z = r.f();
            fxKill(e);
            break;
        }
        case EV_COINS: { float x = r.f(), y = r.f(); int v = r.u32(); int o = r.u8(); spawnCoins(x, y, v, o); break; }
        case EV_FLASK: { float a = r.f(), b = r.f(), c = r.f(), d = r.f(), e = r.f(); int k = r.u8(), o = r.u8(); fxFlask(a, b, c, d, e, k, o); break; }
        case EV_BARR: fxBarricade(r.u8()); break;
        case EV_HURT: { int s = r.u8() % MAX_SLOTS; fxHurt(s, r.f()); break; }
        case EV_RELOAD: { int s = r.u8() % MAX_SLOTS; fxReload(s, r.u8()); break; }
        case EV_HEAL: { int s = r.u8() % MAX_SLOTS; float a = r.f(); fxHealPlayer(s, a, r.u8()); break; }
        case EV_TRAP: { float x = r.f(), y = r.f(); fxTrap(x, y, r.u8()); break; }
        case EV_GLOB: { float x = r.f(), y = r.f(); int k = r.u8(); fxGlobPop(x, y, k, r.u8()); break; }
        case EV_PING: { int s = r.u8() % MAX_SLOTS; float x = r.f(); fxPing(s, x, r.f()); break; }
        case EV_DOWN: fxDown(r.u8() % MAX_SLOTS); break;
        case EV_REVIVE: fxRevive(r.u8() % MAX_SLOTS); break;
        case EV_THUNDER: fxThunder(r.u32()); break;
        case EV_BEAM: { float a = r.f(), b = r.f(), c = r.f(), d = r.f(); fxBeam(a, b, c, d, r.u32()); break; }
        case EV_BURST: { float x = r.f(), y = r.f(); int n = r.u8(); uint32_t c = r.u32(); int k = r.u8(); float s = r.f(); fxBurst(x, y, n, c, k, s, r.f()); break; }
        case EV_PILLAR: { float x = r.f(), y = r.f(); fxPillar(x, y, r.f()); break; }
        case EV_REPAIR: { float x = r.f(); fxRepair(x, r.f()); break; }
        case EV_CLANG: { float x = r.f(); fxClang(x, r.f()); break; }
        default: return; // unknown event: the rest of the batch can't be trusted
        }
        if (!r.ok()) return;
    }
}

// ============================================================ local-only helpers

void Game::addPart(float x, float y, float vx, float vy, float life, uint32_t col, uint8_t kind, float size, float grav, float floorY,
                   float drag) {
    Particle p;
    p.x = x;
    p.y = y;
    p.vx = vx;
    p.vy = vy;
    p.life = p.max = life;
    p.col = col;
    p.kind = kind;
    p.size = size;
    p.grav = grav;
    p.floorY = floorY;
    p.drag = drag;
    p.rest = false;
    parts.push_back(p);
}

void Game::blood(float x, float y, float floorY, uint32_t col, int n, float dirX) {
    float dir = dirX < 0 ? -1.f : 1.f;
    for (int k = 0; k < n; k++)
        addPart(x, y, dir * rnd(10, 90) + rnd(-25, 25), rnd(-110, -10), rnd(1.2f, 2.4f), k % 3 ? col : darker(col), 2, 1, 380,
                floorY + rnd(-2, 3));
}

void Game::gibs(const Enemy& e) {
    const EnemyDef& d = EDEF[e.t];
    SDL_FRect b = enemyBox(e);
    float floorY = d.fly ? rnd(LANE_TOP, LANE_BOT) : e.y;
    uint32_t chunk = 0x6b5a52;
    switch (e.t) {
    case E_PLAGUERAT: chunk = 0x5f7a42; break;
    case E_GIANTRAT: chunk = 0x4a3a34; break;
    case E_RATKING: chunk = 0x5a4a6a; break;
    case E_CROW:
    case E_BAT: chunk = 0x1a1824; break;
    case E_BLOATER:
    case E_SPITTER:
    case E_BRUTE: chunk = 0x7d8f5a; break;
    case E_ABOM: chunk = 0x9a5a5a; break;
    case E_GHOUL: chunk = 0x8a9a88; break;
    case E_KNIGHT: chunk = 0x4a4d58; break;
    case E_CULTIST: chunk = 0x5a2a2a; break;
    case E_BISHOP: chunk = 0xd8b048; break;
    case E_HORSEMAN: chunk = 0xd8d0c0; break;
    default: break;
    }
    int n = std::min(56, int(8 * d.scale * d.scale + (d.h > 10 ? 8 : 0)));
    for (int k = 0; k < n; k++) {
        float size = (d.scale >= 2 && chance(0.5f)) ? 2.f : 1.f;
        addPart(rnd(b.x, b.x + b.w), rnd(b.y, b.y + b.h), rnd(-80, 80), rnd(-170, -40), rnd(1.6f, 3.2f), k % 2 ? chunk : d.blood, 2,
                size, 400, floorY + rnd(-2, 3));
    }
    if (e.t == E_CROW || e.t == E_BAT)
        for (int k = 0; k < 10; k++) addPart(e.x, e.y, rnd(-40, 40), rnd(-60, 10), rnd(1.5f, 2.5f), 0x2a2a44, 0, 1, 25, floorY, 1.5f);
    for (int k = 0; k < int(3 * d.scale); k++)
        decals.push_back({e.x + rnd(-b.w / 2, b.w / 2), floorY + rnd(-2, 2), rnd(1.5f, 3.f) * d.scale, darker(d.blood)});
}

void Game::popup(const std::string& s, float x, float y, uint32_t col, int scale, float life) {
    popups.push_back({s, x, y, life, life, col, scale});
    if (popups.size() > 120) popups.erase(popups.begin());
}

void Game::banner(const std::string& t, const std::string& sub, uint32_t col, float dur) {
    bannerText = t;
    bannerSub = sub;
    bannerCol = col;
    bannerT = bannerMax = dur;
}

void Game::toast(const std::string& s, uint32_t col) {
    toasts.push_back({s, 3.2f, col});
    if (toasts.size() > 5) toasts.erase(toasts.begin());
}

void Game::spawnCoins(float x, float y, int val, int owner) {
    int n = std::clamp(val / 3, 1, 6);
    int each = val / n, rem = val - each * n;
    for (int i = 0; i < n; i++)
        pickups.push_back({x, y, rnd(-45, 45), rnd(-110, -60), rnd(0, 0.15f), clampf(y + rnd(8, 20), LANE_TOP, LANE_BOT + 4),
                           each + (i == 0 ? rem : 0), owner});
}

// Per-entity ambience that every machine generates from the shared world state.
void Game::ambientFx(float dt) {
    if (dt <= 0) return;
    for (auto& e : enemies) {
        if (e.dead) continue;
        SDL_FRect b = enemyBox(e);
        if (e.burn > 0 && chance(dt * 30))
            addPart(rnd(b.x, b.x + b.w), rnd(b.y, b.y + b.h), rnd(-8, 8), rnd(-50, -20), rnd(0.2f, 0.45f), 0xff9030, 4, rnd(3, 6), -30);
        if ((e.t == E_BLOATER || e.t == E_ABOM) && chance(dt * 3))
            addPart(rnd(b.x, b.x + b.w), rnd(b.y, b.y + b.h * 0.7f), rnd(-5, 5), rnd(-20, -8), rnd(0.6f, 1.2f), 0xb0ff50, 6);
        if (e.t == E_BISHOP && chance(dt * 14))
            addPart(rnd(b.x, b.x + b.w), b.y + b.h - rnd(0, 8), rnd(-6, 6), rnd(10, 30), rnd(0.5f, 1.f), chance(0.5f) ? 0xffd860 : 0xc03040, 6);
        if (e.t == E_CULTIST && chance(dt * 4)) addPart(e.x + 5, e.y - 14, rnd(-3, 3), rnd(-14, -6), rnd(0.5f, 0.9f), 0x8aff6a, 6);
        if (e.gas > 0 && chance(dt * 6)) addPart(rnd(b.x, b.x + b.w), b.y, rnd(-4, 4), rnd(-12, -4), 0.5f, 0x9aff5a, 6);
        if (e.t == E_HORSEMAN && e.state == 2 && chance(dt * 30)) addPart(e.x - 24, e.y - rnd(0, 4), rnd(-40, -10), rnd(-30, -5), rnd(0.4f, 0.8f), 0x8a8478, 1, rnd(3, 6), -5, 999, 2);
    }
    for (auto& f : flaskList) {
        if (!chance(dt * 40)) continue;
        float u = f.t / f.dur;
        float x = lerp(f.x0, f.x1, u), y = lerp(f.y0, f.y1, u) - std::sin(PI * u) * 34;
        if (f.kind == 0) addPart(x, y, rnd(-5, 5), rnd(-15, 0), 0.25f, 0xff8030, 4, 3, -20);
        else addPart(x, y, rnd(-5, 5), rnd(-10, 0), 0.4f, 0x9aff5a, 1, 3, -10);
    }
    for (auto& f : fires)
        if (chance(dt * 22)) addPart(f.x + rnd(-6, 6), f.y - rnd(0, 3), rnd(-6, 6), rnd(-45, -20), rnd(0.25f, 0.5f), 0xff9030, 4, rnd(3, 7), -30);
    for (auto& g : gases) {
        float a = clampf(g.life, 0, 1);
        if (chance(dt * 26 * a))
            addPart(g.x + rnd(-g.r, g.r), g.y - rnd(0, 10), rnd(-6, 6), rnd(-10, -2), rnd(0.8f, 1.6f), chance(0.5f) ? 0x7ab040 : 0x9ad850, 1,
                    rnd(8, 16), -3, 999, 1);
    }
    for (auto& g : globs) {
        if (g.dead || !chance(dt * 30)) continue;
        uint32_t c = g.kind == 2 ? 0xff9030 : (g.kind == 1 ? 0xd8d0c0 : 0x8ae030);
        addPart(g.x, g.y, rnd(-5, 5), rnd(-5, 5), 0.3f, c, g.kind == 2 ? 4 : 6, g.kind == 2 ? 3 : 1);
    }
    for (auto& h : hazards) {
        if (h.done || !chance(dt * 30)) continue;
        float a = rnd(0, 2 * PI);
        addPart(h.x + std::cos(a) * h.r, h.y + std::sin(a) * h.r * 0.45f, 0, rnd(-30, -10), 0.3f, 0xff6020, 6);
    }
}

void Game::updateParticles(float dt) {
    for (auto& p : parts) {
        p.life -= dt;
        if (p.rest) continue;
        p.vy += p.grav * dt;
        if (p.drag > 0) {
            float k = std::max(0.f, 1 - p.drag * dt);
            p.vx *= k;
            p.vy *= k;
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.y >= p.floorY) {
            p.y = p.floorY;
            if (p.kind == 2 || p.kind == 5) {
                if (std::fabs(p.vy) < 45) {
                    p.rest = true;
                    p.vx = p.vy = 0;
                    if (p.kind == 2 && chance(0.25f)) decals.push_back({p.x, p.y, 1, darker(p.col)});
                } else {
                    p.vy *= -0.35f;
                    p.vx *= 0.6f;
                    if (p.kind == 5) audio::play(SFX_EMPTY, 0.08f, rnd(1.5f, 2.2f));
                }
            } else {
                p.life = 0;
            }
        }
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0; }), parts.end());
    if (parts.size() > 2500) parts.erase(parts.begin(), parts.begin() + (parts.size() - 2500));

    for (auto& p : popups) {
        p.life -= dt;
        p.y -= 16 * dt;
    }
    popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup& p) { return p.life <= 0; }), popups.end());

    for (auto& t : tlights) t.life -= dt;
    tlights.erase(std::remove_if(tlights.begin(), tlights.end(), [](const TLight& t) { return t.life <= 0; }), tlights.end());
    for (auto& a : arcs) a.life -= dt;
    arcs.erase(std::remove_if(arcs.begin(), arcs.end(), [](const Arc& a) { return a.life <= 0; }), arcs.end());
    for (auto& pg : pings) pg.life -= dt;
    pings.erase(std::remove_if(pings.begin(), pings.end(), [](const Ping& p) { return p.life <= 0; }), pings.end());

    coinPitchT -= dt;
    if (coinPitchT <= 0) coinPitch = 1;
    for (auto& p : pickups) {
        p.t += dt;
        if (p.t < 0.75f) {
            p.vy += 420 * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            if (p.y > p.floorY) {
                p.y = p.floorY;
                p.vy *= -0.45f;
                p.vx *= 0.6f;
            }
            continue;
        }
        // Coins fly to the purse of whoever earned them: the HUD for us, the doctor for colleagues.
        bool mine = p.owner == localSlot;
        bool valid = p.owner >= 0 && p.owner < MAX_SLOTS && players[p.owner].active;
        float tx = mine ? HUD_COIN_X : (valid ? players[p.owner].x : VW + 20);
        float ty = mine ? HUD_COIN_Y : (valid ? players[p.owner].y - 14 : 0);
        float dx = tx - p.x, dy = ty - p.y, d = std::hypot(dx, dy);
        float sp = 120 + (p.t - 0.75f) * 900;
        if (d < sp * dt + 3) {
            if (authority() && valid) players[p.owner].coins += p.val;
            p.val = -1;
            if (mine) {
                coinPulse = 0.15f;
                audio::play(SFX_COIN, 0.22f, coinPitch);
                coinPitch = std::min(2.2f, coinPitch + 0.05f);
                coinPitchT = 0.4f;
            }
        } else {
            p.x += dx / d * sp * dt;
            p.y += dy / d * sp * dt;
        }
    }
    pickups.erase(std::remove_if(pickups.begin(), pickups.end(), [](const Pickup& p) { return p.val < 0; }), pickups.end());
}
