// Authoritative combat simulation: enemies, bosses, projectiles, hazards. Runs offline and on the host.
#include "Defs.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>

using namespace util;

// ============================================================ helpers

SDL_FRect Game::enemyBox(const Enemy& e) const {
    const EnemyDef& d = EDEF[e.t];
    float w = d.w * d.scale, h = d.h * d.scale;
    if (d.fly) return {e.x - w / 2, e.y - h / 2, w, h};
    return {e.x - w / 2, e.y - h - e.z, w, h};
}

SDL_FRect Game::playerBox(const Player& p) const { return {p.x - 6, p.y - 28, 12, 28}; }

int Game::pickTarget(float x, float y, bool random) const {
    int alive[MAX_SLOTS], n = 0;
    for (int i = 0; i < MAX_SLOTS; i++)
        if (players[i].alive()) alive[n++] = i;
    if (n == 0) return -1;
    if (random) return alive[irnd(0, n - 1)];
    int best = alive[0];
    float bd = 1e9f;
    for (int k = 0; k < n; k++) {
        const Player& p = players[alive[k]];
        float d = std::hypot(p.x - x, (p.y - y) * 1.5f);
        if (d < bd) { bd = d; best = alive[k]; }
    }
    return best;
}

// Where a ground enemy stops to attack: the stakes while they stand, otherwise its chosen doctor.
float Game::enemyStopX(const Enemy& e) const {
    const EnemyDef& d = EDEF[e.t];
    float halfW = d.w * d.scale * 0.5f;
    if (barrHp > 0 && !e.over) return BARR_X - 6 - halfW;
    if (e.tgt < 0 || !players[e.tgt].alive()) return VW + 60;
    return players[e.tgt].x - 9 - halfW;
}

// ============================================================ spawning & damage

Enemy& Game::spawnEnemy(EType t, float x, float y, bool deferred) {
    const EnemyDef& d = EDEF[t];
    Enemy e;
    e.id = nextId++;
    if (nextId > 60000) nextId = 1;
    e.t = t;
    float hpMul = 1 + 0.12f * (wave - 1) + (wave > 12 ? 0.08f * (wave - 12) : 0.f);
    hpMul *= isBoss(t) ? 1 + 0.7f * (std::max(1, activeCount()) - 1) : hpScale();
    if (isBoss(t)) hpMul = std::max(1.f, hpMul * 0.55f); // boss base values already include their wave
    e.hp = e.maxHp = d.hp * hpMul * (1 + 0.5f * loop);
    e.speed = d.speed * rnd(0.88f, 1.12f) * (1 + std::min(0.35f, wave * 0.012f));
    e.dmg = d.dmg * (1 + 0.05f * (wave - 1));
    e.atkT = d.atkInt * 0.5f;
    e.x = x;
    e.y = y;
    e.anim = rnd(0, 10);
    e.tgt = pickTarget(x, y, true);
    switch (t) {
    case E_CROW:
        e.y = e.hoverY = rnd(30, 90);
        e.standX = rnd(150, 260);
        break;
    case E_BAT:
        e.y = e.hoverY = rnd(60, 120);
        e.standX = rnd(190, 280);
        e.x = x - rnd(0, 30);
        break;
    case E_SPITTER: e.standX = rnd(110, 190); break;
    case E_CULTIST: e.standX = rnd(60, 150); break;
    case E_BISHOP:
        e.y = e.hoverY = 96;
        e.standX = 170;
        e.stT = -2;
        break;
    case E_HORSEMAN: e.y = clampf(y, LANE_TOP + 8, LANE_BOT - 4); break;
    default: break;
    }
    auto& vec = deferred ? pending : enemies;
    vec.push_back(e);
    if (deferred) waveTotal++;
    if (isBoss(t)) sfx(t == E_HORSEMAN ? SFX_NEIGH : (t == E_BISHOP ? SFX_CHANT : SFX_GROAN), 0.9f, t == E_RATKING || t == E_ABOM ? 0.5f : 1.f);
    return vec.back();
}

void Game::damageEnemy(Enemy& e, float dmg, bool crit, float knock, int by, bool showPopup) {
    if (e.dead) return;
    const EnemyDef& d = EDEF[e.t];
    if (e.t == E_HORSEMAN && e.state == 3) dmg *= 1.75f;
    if (e.mark > 0) dmg *= 1.15f;
    if (by >= 0) {
        // Crossfire: hitting a foe a colleague just hit deals bonus damage and earns them an assist.
        if (e.lastHit >= 0 && e.lastHit != by && time - e.lastHitT < 1.5f) {
            dmg *= 1.2f;
            e.assist = e.lastHit;
            if (showPopup && chance(0.08f)) fxPopup("CROSSFIRE!", e.x, enemyBox(e).y - 14, 0xff9a30, 1, 0.9f);
        }
        e.lastHit = by;
        e.lastHitT = time;
        players[by].dmgDealt += dmg;
    }
    e.hp -= dmg;
    e.flash = 0.07f;
    e.knock += knock * (1 - d.knockRes);
    if (showPopup) {
        SDL_FRect b = enemyBox(e);
        fxDamage(e.x + (crit ? 0 : rnd(-4, 4)), b.y - (crit ? 6 : 3), dmg, crit);
    }
    if (e.hp <= 0) killEnemy(e, crit, by);
}

void Game::killEnemy(Enemy& e, bool crit, int by) {
    if (e.dead) return;
    e.dead = true;
    const EnemyDef& d = EDEF[e.t];
    waveKilled++;
    int owner = by >= 0 ? by : e.lastHit;
    if (owner < 0 || owner >= MAX_SLOTS || !players[owner].active) {
        owner = 0;
        for (int i = MAX_SLOTS - 1; i >= 0; i--)
            if (players[i].active) owner = i;
    }
    Player& k = players[owner];
    k.kills++;
    k.combo++;
    k.comboT = 2.2f;
    k.bestCombo = std::max(k.bestCombo, k.combo);
    if (k.combo >= 5 && k.combo % 5 == 0) sfx(SFX_COIN, 0.4f, 1.5f + std::min(k.combo, 30) * 0.02f);

    SDL_FRect b = enemyBox(e);
    float cy = b.y + b.h / 2;
    float comboMul = 1 + std::min(k.combo, 20) * 0.05f;
    int val = std::max(1, int(std::round(d.coins * coinMul(k) * comboMul)));
    fxCoins(e.x, cy, val, owner);
    if (e.assist >= 0 && e.assist != owner && players[e.assist].active) {
        fxCoins(e.x, cy, std::max(1, int(val * 0.35f)), e.assist);
        fxPopup("ASSIST", e.x, b.y - 12, SLOT_COL[e.assist], 1, 0.7f);
    }
    fxKill(e);

    if (e.t == E_PLAGUERAT) {
        for (int i = 0; i < 3; i++) spawnEnemy(E_RATLING, e.x + rnd(-6, 6), clampf(e.y + rnd(-7, 7), LANE_TOP, LANE_BOT), true);
        fxBurst(e.x, cy, 8, 0x7aa040, 1, 30, 1.f);
    }
    if (e.t == E_BLOATER || e.t == E_ABOM) {
        float r = e.t == E_ABOM ? 70.f : 44.f;
        explode(e.x, cy, r, (e.t == E_ABOM ? 150.f : 70.f) * (1 + 0.05f * wave), e.t == E_ABOM, owner);
        fxBurst(e.x, cy, 14, 0x6a8a3a, 1, 25, 2.f);
    }
    if (isBoss(e.t)) {
        bossesThisRun++;
        timeScale = 0.2f;
        hitstop = std::max(hitstop, 0.12f);
        fxShake(8);
        fxBanner("BOSS SLAIN!", std::string(d.name) + " IS NO MORE", 0xffd860, 3.f);
        // Every doctor gets a share of the hoard.
        for (int i = 0; i < MAX_SLOTS; i++)
            if (players[i].active)
                for (int c = 0; c < 4; c++) fxCoins(e.x + rnd(-20, 20), cy, int(val / 6) + 1, i);
        hazards.clear();
    } else if (d.scale >= 2 || e.t == E_BRUTE || e.t == E_KNIGHT) {
        hitstop = std::max(hitstop, 0.05f);
        fxShake(2);
    } else if (crit) {
        hitstop = std::max(hitstop, 0.02f);
    }
}

void Game::explode(float x, float y, float r, float dmg, bool big, int by) {
    for (auto& e : enemies) {
        if (e.dead) continue;
        SDL_FRect b = enemyBox(e);
        float ex = b.x + b.w / 2, ey = b.y + b.h / 2;
        float dd = std::hypot(ex - x, (ey - y) * 1.3f);
        if (dd < r + b.w * 0.4f) {
            float f = 1 - 0.5f * clampf(dd / r, 0, 1);
            damageEnemy(e, dmg * f, false, ex < x ? 140.f : -140.f, by, true);
            if (!e.dead) e.burn = std::max(e.burn, 1.0f);
        }
    }
    for (auto& g : globs)
        if (g.kind != 1 && std::hypot(g.x - x, g.y - y) < r) g.dead = true;
    fxExplode(x, y, r, big);
    hitstop = std::max(hitstop, 0.035f);
}

void Game::hitBarricade(float d) {
    if (barrHp <= 0) return;
    barrHp -= d;
    if (barrHp <= 0) {
        barrHp = 0;
        fxBarricade(true);
        fxBanner("BARRICADE BROKEN!", "THEY ARE COMING FOR YOU", 0xff4a4a, 2.2f);
    } else {
        fxBarricade(false);
    }
}

void Game::hurtPlayer(int slot, float d) {
    if (slot < 0 || slot >= MAX_SLOTS) return;
    Player& p = players[slot];
    if (!p.alive() || p.invulnT > 0 || mode != Mode::Play || dying) return;
    p.hp -= d;
    p.combo = 0;
    p.comboT = 0;
    fxHurt(slot, d);
    if (p.hp <= 0) downPlayer(slot);
}

void Game::downPlayer(int slot) {
    Player& p = players[slot];
    p.hp = 0;
    p.down = true;
    p.downT = p.reviveT = 0;
    p.reloading = false;
    p.downs++;
    if (slot == localSlot) lmb = false;
    // Enemies that were chewing on this doctor look for someone else.
    for (auto& e : enemies)
        if (e.tgt == slot) e.tgt = pickTarget(e.x, e.y);
    if (aliveCount() > 0) {
        fxDown(slot);
        fxToast(p.name + " IS DOWN! HOLD F NEAR THEM TO REVIVE", 0xff6050);
    }
}

void Game::spit(float x, float y, float flight, int target, int kind) {
    float tx = 346, ty = 176;
    if (target >= 0) {
        tx = players[target].x + rnd(-3, 3);
        ty = players[target].y - 14;
    }
    if (kind == 1) {
        // Scythes fly straight.
        float dx = tx - x, dy = ty - y, d = std::max(1.f, std::hypot(dx, dy));
        globs.push_back({x, y, dx / d * 170, dy / d * 170, 1, false});
        sfx(SFX_SAW, 0.5f, 0.6f);
        return;
    }
    const float g = 200;
    globs.push_back({x, y, (tx - x) / flight, (ty - y) / flight - 0.5f * g * flight, kind, false});
    sfx(kind == 2 ? SFX_PILLAR : SFX_SPIT, kind == 2 ? 0.25f : 0.5f, kind == 2 ? 1.8f : rnd(0.8f, 1.2f));
}

// ============================================================ enemies

void Game::updateEnemies(float dt) {
    for (size_t i = 0; i < enemies.size(); i++) {
        Enemy& e = enemies[i];
        if (e.dead) continue;
        const EnemyDef& d = EDEF[e.t];
        e.flash -= dt;
        e.mark -= dt;
        e.lunge = std::max(0.f, e.lunge - dt * 5);
        if (e.burn > 0) {
            e.burn -= dt;
            e.hp -= 10 * dt * (1 + 0.05f * wave);
            if (e.hp <= 0) {
                killEnemy(e, false, e.lastHit);
                continue;
            }
        }
        if (e.knock != 0) {
            e.x -= e.knock * dt;
            e.knock = approach(e.knock, 0, 520 * dt);
        }
        if (e.tgt < 0 || !players[e.tgt].alive()) e.tgt = pickTarget(e.x, e.y);
        if (e.t == E_BISHOP || e.t == E_HORSEMAN) {
            updateBoss(e, dt);
            continue;
        }
        if (d.fly) {
            updateFlyer(e, dt);
            continue;
        }
        if (e.trapT > 0) {
            e.trapT -= dt;
            continue;
        }
        if (e.stun > 0) {
            e.stun -= dt;
            continue;
        }
        float spd = e.speed * (e.gas > 0 ? 0.55f : 1.f);
        e.gas -= dt;
        bool past = barrHp <= 0 || e.over;
        if (past && e.tgt >= 0) {
            const Player& t = players[e.tgt];
            e.y = approach(e.y, clampf(t.y, LANE_TOP, LANE_BOT), spd * 0.6f * dt);
        }
        float stop = enemyStopX(e);
        e.attacking = false;
        auto strike = [&](float dmg) {
            e.lunge = 1;
            if (barrHp > 0 && !e.over) hitBarricade(dmg);
            else hurtPlayer(e.tgt, dmg);
        };

        if (e.t == E_GHOUL && e.state == 1) {
            // Mid-leap over the stakes.
            e.stT += dt;
            float u = clampf(e.stT / 0.75f, 0, 1);
            e.x = lerp(e.standX, e.hoverY, u);
            e.z = std::sin(PI * u) * 30;
            if (u >= 1) {
                e.z = 0;
                e.over = true;
                e.state = 2;
                fxBurst(e.x, e.y, 6, 0x6a6474, 0, 40, 0.4f);
            }
            continue;
        }
        if (e.t == E_GHOUL && e.state == 0 && barrHp > 0 && !e.over && e.x >= BARR_X - 46) {
            e.state = 1;
            e.stT = 0;
            e.standX = e.x;
            e.hoverY = BARR_X + 12 + rnd(0, 14);
            sfx(SFX_LEAP, 0.6f, rnd(0.9f, 1.1f));
            continue;
        }
        bool inReach = !past || e.tgt < 0 || std::fabs(players[e.tgt].y - e.y) < 9;
        if ((e.t == E_SPITTER && e.x >= e.standX) || (e.t == E_CULTIST && e.x >= e.standX)) {
            e.attacking = true;
            e.atkT -= dt;
            if (e.atkT <= 0) {
                e.atkT = d.atkInt * rnd(0.8f, 1.2f) * (e.t == E_CULTIST ? 3.2f : 1.f);
                e.lunge = 1;
                if (e.t == E_SPITTER) {
                    spit(e.x + 6, e.y - 14, rnd(1.0f, 1.25f), pickTarget(e.x, e.y, true));
                } else {
                    // Cultists mend their flock, or lob cursed embers when there is nobody to mend.
                    int healed = 0;
                    for (auto& o : enemies) {
                        if (&o == &e || o.dead || o.hp >= o.maxHp || std::hypot(o.x - e.x, o.y - e.y) > 70) continue;
                        o.hp = std::min(o.maxHp, o.hp + o.maxHp * (isBoss(o.t) ? 0.03f : 0.15f));
                        if (healed++ < 4) fxBeam(e.x + 4, e.y - 16, o.x, enemyBox(o).y + 3, 0x8aff6a);
                    }
                    if (healed) sfx(SFX_HEAL, 0.5f);
                    else spit(e.x + 4, e.y - 16, 1.2f, pickTarget(e.x, e.y, true), 2);
                }
            }
        } else if (e.x < stop - 0.5f || !inReach) {
            if (e.x < stop - 0.5f) e.x = std::min(stop, e.x + spd * dt);
            e.anim += dt * (spd * 0.13f + 2);
        } else {
            if (e.x > stop + 3) e.x -= spd * dt;
            else e.x = std::max(e.x, stop);
            e.attacking = true;
            if (e.t == E_BLOATER) {
                if (barrHp > 0 && !e.over) hitBarricade(35 + wave * 2.f);
                else hurtPlayer(e.tgt, 20 + wave * 0.5f);
                e.dead = true;
                waveKilled++;
                explode(e.x, e.y - 9, 40, 50, false, -1);
                continue;
            }
            e.atkT -= dt;
            if (e.atkT <= 0) {
                e.atkT = d.atkInt;
                strike(e.dmg);
            }
        }
        if (isBoss(e.t)) updateBoss(e, dt);
        bool mutant = e.t == E_BLOATER || e.t == E_BRUTE || e.t == E_SPITTER || e.t == E_ABOM;
        if (mutant && chance(dt * 0.08f)) sfx(SFX_GROAN, 0.25f, rnd(0.7f, 1.1f) * (e.t == E_ABOM ? 0.5f : 1.f));
    }
}

void Game::updateFlyer(Enemy& e, float dt) {
    bool bat = e.t == E_BAT;
    e.anim += dt * (bat ? 18 : 12);
    float wob = bat ? std::sin(time * 9 + e.id) * 14 : 0;
    switch (e.state) {
    case 0: {
        float tx = e.standX, ty = e.hoverY + std::sin(time * 3 + e.id) * 6 + wob;
        float dx = tx - e.x, dy = ty - e.y, dist = std::hypot(dx, dy);
        if (dist > 0.01f) {
            e.vx = dx / dist * e.speed;
            e.vy = dy / dist * e.speed;
        }
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        if (dist < 6) {
            e.state = 1;
            e.stT = bat ? rnd(0.2f, 0.8f) : 0.6f;
            sfx(bat ? SFX_SCREECH : SFX_CAW, bat ? 0.25f : 0.4f, rnd(0.9f, 1.2f));
        }
        break;
    }
    case 1:
        e.stT -= dt;
        e.vx = 10;
        e.y += std::sin(time * 30) * 0.3f;
        if (e.stT <= 0) {
            e.state = 2;
            e.tgt = pickTarget(e.x, e.y, true);
            float tx = e.tgt >= 0 ? players[e.tgt].x : 346, ty = e.tgt >= 0 ? players[e.tgt].y - 20 : 170;
            float dx = tx - e.x, dy = ty - e.y, dd = std::max(1.f, std::hypot(dx, dy));
            float sp = (bat ? 190 : 150) + wave * 3.f;
            e.vx = dx / dd * sp;
            e.vy = dy / dd * sp;
        }
        break;
    case 2: {
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        bool hit = false;
        for (int i = 0; i < MAX_SLOTS; i++) {
            const Player& p = players[i];
            if (!p.alive()) continue;
            if (std::hypot(e.x - p.x, e.y - (p.y - 20)) < 9) {
                hurtPlayer(i, e.dmg);
                hit = true;
                break;
            }
        }
        if (hit || e.x > VW + 10 || e.y > VH) {
            e.state = 3;
            e.stT = bat ? 0.8f : 1.2f;
            e.vx = -90;
            e.vy = -110;
        }
        break;
    }
    case 3:
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        e.vy += 50 * dt;
        e.stT -= dt;
        if (e.stT <= 0) {
            e.state = 0;
            e.standX = bat ? rnd(180, 285) : rnd(130, 260);
            e.hoverY = bat ? rnd(60, 125) : rnd(30, 90);
        }
        break;
    }
}

void Game::updateBoss(Enemy& e, float dt) {
    switch (e.t) {
    case E_RATKING:
        e.stT += dt;
        if (e.stT > 5.5f) {
            e.stT = 0;
            for (int k = 0; k < 3 + std::min(2, activeCount() - 1); k++)
                spawnEnemy(E_RAT, e.x - rnd(4, 24), clampf(e.y + rnd(-12, 12), LANE_TOP, LANE_BOT), true);
            sfx(SFX_SQUEAK, 0.8f, 0.55f);
            fxPopup("RISE, MY CHILDREN!", e.x, e.y - 34, 0xd0a0ff, 1, 1.4f);
            fxBurst(e.x, e.y - 8, 10, 0x6a4a8a, 1, 40, 1.f);
        }
        break;
    case E_ABOM:
        e.stT += dt;
        e.stT2 += dt;
        if (e.stT > 8) {
            e.stT = 0;
            spawnEnemy(E_BLOATER, e.x - 14, clampf(e.y + rnd(-10, 10), LANE_TOP, LANE_BOT), true);
        }
        if (e.stT2 > 4.5f) {
            e.stT2 = 0;
            for (int k = 0; k < 3; k++) spit(e.x - 2, e.y - 32, 0.9f + k * 0.22f, pickTarget(e.x, e.y, true));
        }
        break;
    case E_BISHOP: {
        // Floats above the street, calls down judgement on every doctor, summons the faithful and blinks around.
        e.anim += dt * 3;
        bool enraged = e.hp < e.maxHp * 0.5f;
        if (e.state == 0) {
            e.x = approach(e.x, e.standX, 40 * dt);
            if (std::fabs(e.x - e.standX) < 1) e.state = 1;
        }
        e.y = e.hoverY + std::sin(time * 1.6f) * 5;
        if (e.state != 1) break;
        e.stT += dt;
        e.stT2 += dt;
        e.stT3 += dt;
        e.atkT -= dt;
        if (e.stT > (enraged ? 3.6f : 5.f)) {
            e.stT = 0;
            for (int i = 0; i < MAX_SLOTS; i++) {
                const Player& p = players[i];
                if (!p.alive()) continue;
                hazards.push_back({p.x, p.y, 15, 0, enraged ? 1.1f : 1.35f, 24 + wave * 0.6f, false});
                if (enraged) hazards.push_back({clampf(p.x + rnd(-24, 24), YARD_X0, YARD_X1), clampf(p.y + rnd(-10, 10), YARD_Y0, YARD_Y1),
                                                13, 0, 1.5f, 20 + wave * 0.5f, false});
            }
            fxPopup(enraged ? "BURN, HERETICS!" : "JUDGEMENT!", e.x, e.y - 40, 0xff7040, 1, 1.2f);
            sfx(SFX_CHANT, 0.7f, enraged ? 1.15f : 1.f);
        }
        if (e.stT2 > 11) {
            e.stT2 = 0;
            for (int k = 0; k < 2; k++) spawnEnemy(E_CULTIST, -10 - k * 12, rnd(LANE_TOP, LANE_BOT), true);
            for (int k = 0; k < 3; k++) spawnEnemy(E_RAT, -20 - k * 8, rnd(LANE_TOP, LANE_BOT), true);
            fxPopup("KNEEL, SINNERS!", e.x, e.y - 40, 0xd0a0ff, 1, 1.4f);
        }
        if (e.stT3 > 8) {
            e.stT3 = 0;
            fxBurst(e.x, e.y, 22, 0xc03040, 4, 60, 0.6f);
            e.x = e.standX = rnd(110, 250);
            e.hoverY = rnd(82, 108);
            e.y = e.hoverY;
            fxBurst(e.x, e.y, 22, 0xffd860, 4, 60, 0.6f);
            sfx(SFX_ZAP, 0.5f, 0.6f);
        }
        if (e.atkT <= 0) {
            e.atkT = enraged ? 1.4f : 2.2f;
            spit(e.x + 10, e.y - 6, 1.1f, pickTarget(e.x, e.y, true), 2);
        }
        break;
    }
    case E_HORSEMAN: {
        if (e.trapT > 0) {
            e.trapT -= dt;
            break;
        }
        e.attacking = e.state == 2;
        auto gallop = [&](float rate) {
            e.anim += dt * rate;
            e.stT2 -= dt;
            if (e.stT2 <= 0) {
                e.stT2 = 0.28f;
                sfx(SFX_GALLOP, 0.35f, rnd(0.9f, 1.1f));
            }
        };
        switch (e.state) {
        case 0: // ride in
            e.x += 60 * dt;
            gallop(8);
            if (e.x >= 50) {
                e.state = 1;
                e.stT = 0;
            }
            break;
        case 1: // wind-up before a charge
            e.stT += dt;
            if (e.stT - dt <= 0) {
                sfx(SFX_NEIGH, 0.8f, rnd(0.9f, 1.05f));
                fxPopup("!", e.x + 20, e.y - 54, 0xff4040, 3, 1.f);
            }
            if (e.stT > 1.25f) {
                e.state = 2;
                e.mask = 0;
            }
            break;
        case 2: { // charge
            e.x += (230 + wave * 2.f) * dt;
            gallop(16);
            if (chance(dt * 40)) fxBurst(e.x - 20, e.y, 1, 0x8a8478, 0, 30, 0.5f);
            if (barrHp > 0) {
                float stop = BARR_X - 6 - EDEF[e.t].w;
                if (e.x >= stop) {
                    e.x = stop;
                    hitBarricade(70 + wave * 1.5f);
                    fxShake(6);
                    e.state = 3;
                    e.stT = 0;
                    fxPopup("STUNNED!", e.x, e.y - 54, 0xffd860, 1, 1.4f);
                }
            } else {
                for (int i = 0; i < MAX_SLOTS; i++) {
                    const Player& p = players[i];
                    if (!p.alive() || (e.mask & (1 << i))) continue;
                    if (std::fabs(p.y - e.y) < 14 && p.x > e.x - 20 && p.x < e.x + 30) {
                        e.mask |= 1 << i;
                        hurtPlayer(i, 28 + wave * 0.6f);
                    }
                }
                if (e.x >= YARD_X0 + 18) {
                    e.state = 3;
                    e.stT = 0;
                    fxPopup("STUNNED!", e.x, e.y - 54, 0xffd860, 1, 1.4f);
                }
            }
            break;
        }
        case 3: // stunned: takes extra damage
            e.stT += dt;
            if (e.stT > 3.2f) {
                e.state = 4;
                e.stT = 0;
                e.hoverY = rnd(LANE_TOP + 8, LANE_BOT - 4);
            }
            break;
        case 4: // retreat, hurling scythes
            e.x -= 75 * dt;
            e.y = approach(e.y, e.hoverY, 20 * dt);
            gallop(10);
            e.stT += dt;
            if (e.stT > 1.1f) {
                e.stT = 0;
                spit(e.x + 18, e.y - 40, 1.f, pickTarget(e.x, e.y, true), 1);
            }
            if (e.x <= 40) {
                e.state = 1;
                e.stT = 0;
            }
            break;
        }
        break;
    }
    default: break;
    }
}

// ============================================================ projectiles & misc

void Game::updateBullets(float dt) {
    for (auto& b : bullets) {
        if (b.dead) continue;
        b.life -= dt;
        if (b.life <= 0) { b.dead = true; continue; }
        if (b.kind == K_FLAME) {
            float k = std::max(0.f, 1 - 2.2f * dt);
            b.vx *= k;
            b.vy = b.vy * k - 25 * dt;
        } else if (b.kind == K_SAW) {
            b.vy += 380 * dt;
            b.spin += dt * 30;
        }
        float len = std::hypot(b.vx, b.vy) * dt;
        int steps = std::max(1, int(len / 3) + 1);
        for (int s = 0; s < steps && !b.dead; s++) {
            b.x += b.vx * dt / steps;
            b.y += b.vy * dt / steps;
            if (b.kind == K_SAW && b.y >= b.floorY) {
                b.y = b.floorY;
                if (b.vy > 60) {
                    b.vy *= -0.45f;
                    fxBurst(b.x, b.y, 2, 0xfff0a0, 3, 60, 0.15f);
                } else {
                    b.vy = 0;
                }
                b.vx *= 1 - 0.25f * dt / steps;
            }
            if (b.kind != K_FLAME && !b.cosmetic) {
                for (auto& g : globs) {
                    if (g.dead || g.kind == 1 || std::fabs(g.x - b.x) > 4 || std::fabs(g.y - b.y) > 4) continue;
                    g.dead = true;
                    b.dead = true;
                    if (b.owner >= 0) players[b.owner].coins += 1;
                    fxGlobPop(g.x, g.y, g.kind, true);
                    break;
                }
                if (b.dead) break;
            }
            for (auto& e : enemies) {
                if (e.dead) continue;
                bool already = false;
                for (int k = 0; k < b.nh; k++) already |= b.hits[k] == e.id;
                if (already) continue;
                SDL_FRect bx = enemyBox(e);
                if (b.x < bx.x || b.x > bx.x + bx.w || b.y < bx.y || b.y > bx.y + bx.h) continue;
                if (b.nh < 8) b.hits[b.nh++] = e.id;
                if (b.cosmetic) {
                    // Client-side copies only stop where the host's real shot would.
                    if (b.kind == K_FLAME) continue;
                    if (b.blast > 0 || --b.pierce < 0) { b.dead = true; break; }
                    continue;
                }
                if (b.kind == K_FLAME) {
                    e.burn = std::max(e.burn, 2.6f);
                    damageEnemy(e, b.dmg, false, 0, b.owner, false);
                    b.dmg *= 0.7f;
                    continue;
                }
                bool crit = b.y < bx.y + bx.h * 0.34f && b.kind != K_SAW;
                float dmg = b.dmg * (crit ? critMul(players[b.owner]) : 1.f);
                // Plague knights hide behind tower shields: only headshots, saws and blasts get through.
                if (e.t == E_KNIGHT && !crit && b.kind != K_SAW && b.blast <= 0 && b.vx < 0) {
                    damageEnemy(e, dmg * 0.15f, false, b.knock * 0.2f, b.owner, false);
                    fxClang(b.x, b.y);
                    b.dead = true;
                    break;
                }
                fxHit(e.id, b.x, b.y, e.y, EDEF[e.t].blood, crit, b.vx);
                damageEnemy(e, dmg, crit, b.vx < 0 ? b.knock : -b.knock * 0.3f, b.owner, true);
                if (b.kind == K_LEECH && b.owner >= 0) {
                    // Leeches feed the shooter and the most wounded colleague.
                    healPlayer(b.owner, 0.6f, false);
                    int worst = -1;
                    float wf = 1;
                    for (int i = 0; i < MAX_SLOTS; i++) {
                        const Player& p = players[i];
                        if (i == b.owner || !p.alive() || p.hp / p.maxHp >= wf) continue;
                        wf = p.hp / p.maxHp;
                        worst = i;
                    }
                    if (worst >= 0) healPlayer(worst, 0.9f, false);
                }
                if (b.blast > 0) {
                    explode(b.x, b.y, b.blast, b.dmg * 0.8f, false, b.owner);
                    b.dead = true;
                    break;
                }
                if (--b.pierce < 0) { b.dead = true; break; }
                if (b.kind != K_SAW) b.dmg *= b.weapon == 5 ? 0.92f : 0.8f;
            }
        }
        if (b.dead) continue;
        if (b.x < -30 || b.x > VW + 30 || b.y < -60) b.dead = true;
        else if (b.kind != K_FLAME && b.kind != K_SAW && b.y > LANE_BOT + 6) {
            b.dead = true;
            fxBurst(b.x, b.y, 4, 0x6a6474, 0, 40, 0.35f);
            if (b.blast > 0 && !b.cosmetic) explode(b.x, LANE_BOT, b.blast, b.dmg * 0.8f, false, b.owner);
        }
    }
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return b.dead; }), bullets.end());
}

void Game::fireLightning(int slot, float x, float y, float dmg) {
    const Player& p = players[slot];
    std::vector<std::pair<float, float>> pts{{x, y}};
    Enemy* cur = nullptr;
    float best = 1e9f;
    for (auto& e : enemies) {
        if (e.dead) continue;
        SDL_FRect b = enemyBox(e);
        float ex = b.x + b.w / 2, ey = b.y + b.h / 2;
        float d = std::hypot(ex - x, ey - y);
        float ad = std::fabs(std::remainder(std::atan2(ey - y, ex - x) - p.aimAng, 2 * PI));
        if (d < 240 && ad < 0.32f && d < best) {
            best = d;
            cur = &e;
        }
    }
    if (!cur) {
        float len = clampf(std::hypot(p.in.aimX - x, p.in.aimY - y), 30, 140);
        pts.push_back({x + std::cos(p.aimAng) * len, y + std::sin(p.aimAng) * len});
        fxArc(pts, 0x8ad8ff);
        return;
    }
    std::vector<int> hit;
    for (int chain = 0; chain <= WEAPONS[6].pierce && cur; chain++) {
        SDL_FRect b = enemyBox(*cur);
        float ex = b.x + b.w / 2, ey = b.y + b.h / 2;
        pts.push_back({ex, ey});
        hit.push_back(cur->id);
        cur->stun = std::max(cur->stun, 0.22f);
        damageEnemy(*cur, dmg, false, 15, slot, true);
        dmg *= 0.8f;
        Enemy* next = nullptr;
        float nd = 70;
        for (auto& e : enemies) {
            if (e.dead || std::find(hit.begin(), hit.end(), e.id) != hit.end()) continue;
            SDL_FRect o = enemyBox(e);
            float d = std::hypot(o.x + o.w / 2 - ex, o.y + o.h / 2 - ey);
            if (d < nd) {
                nd = d;
                next = &e;
            }
        }
        cur = next;
    }
    fxArc(pts, 0x8ad8ff);
}

void Game::updateMisc(float dt) {
    // Flasks and gas canisters in flight
    for (auto& f : flaskList) {
        f.t += dt;
        if (f.t < f.dur || f.cosmetic) continue;
        if (f.kind == 0) {
            float dm = f.owner >= 0 ? dmgMul(players[f.owner]) : 1.f;
            explode(f.x1, f.y1, 40, 70 * dm, true, f.owner);
            for (int k = 0; k < 3; k++) fires.push_back({f.x1 + rnd(-14, 14), clampf(f.y1 + rnd(-6, 6), LANE_TOP, LANE_BOT), rnd(3, 4.5f)});
            fxBurst(f.x1, f.y1 - 2, 10, 0xb0d8e8, 2, 90, 1.f);
        } else {
            float dm = f.owner >= 0 ? dmgMul(players[f.owner]) : 1.f;
            gases.push_back({f.x1, f.y1, WEAPONS[8].blast, 5.f, WEAPONS[8].dmg * dm, f.owner});
            fxBurst(f.x1, f.y1 - 4, 16, 0x9aff5a, 1, 40, 1.4f);
            sfx(SFX_GAS, 0.6f, rnd(0.9f, 1.1f));
        }
    }
    flaskList.erase(std::remove_if(flaskList.begin(), flaskList.end(), [](const Flask& f) { return f.t >= f.dur; }), flaskList.end());

    // Traps
    for (auto& tr : trapList) {
        if (tr.shut) { tr.t += dt; continue; }
        for (auto& e : enemies) {
            const EnemyDef& d = EDEF[e.t];
            if (e.dead || d.fly || e.z > 4) continue;
            if (std::fabs(e.x - tr.x) > 7 + d.w * d.scale * 0.25f || std::fabs(e.y - tr.y) > 8) continue;
            tr.shut = true;
            e.trapT = isBoss(e.t) ? 0.8f : (e.t == E_BRUTE || e.t == E_KNIGHT ? 1.8f : 3.2f);
            fxTrap(tr.x, tr.y, true);
            damageEnemy(e, 45, false, 0, -1, true);
            break;
        }
    }
    trapList.erase(std::remove_if(trapList.begin(), trapList.end(), [](const Trap& t) { return t.shut && t.t > 3.5f; }), trapList.end());

    // Ground fires
    for (auto& f : fires) {
        f.life -= dt;
        for (auto& e : enemies)
            if (!e.dead && !EDEF[e.t].fly && std::fabs(e.x - f.x) < 12 && std::fabs(e.y - f.y) < 7) e.burn = std::max(e.burn, 1.2f);
    }
    fires.erase(std::remove_if(fires.begin(), fires.end(), [](const FirePatch& f) { return f.life <= 0; }), fires.end());

    // Plague gas
    for (auto& g : gases) {
        g.life -= dt;
        for (auto& e : enemies) {
            if (e.dead) continue;
            SDL_FRect b = enemyBox(e);
            float ex = b.x + b.w / 2, ey = b.y + b.h;
            if (std::hypot(ex - g.x, (ey - g.y) * 1.6f) > g.r + b.w * 0.3f) continue;
            e.gas = 0.3f;
            damageEnemy(e, g.dps * dt * (isBoss(e.t) ? 0.7f : 1.f), false, 0, g.owner, false);
        }
    }
    gases.erase(std::remove_if(gases.begin(), gases.end(), [](const GasCloud& g) { return g.life <= 0; }), gases.end());

    // Globs, scythes and hellfire orbs
    for (auto& g : globs) {
        if (g.dead) continue;
        if (g.kind != 1) g.vy += 200 * dt;
        g.x += g.vx * dt;
        g.y += g.vy * dt;
        for (int i = 0; i < MAX_SLOTS && !g.dead; i++) {
            const Player& p = players[i];
            if (!p.alive()) continue;
            if (std::fabs(g.x - p.x) < 7 && g.y > p.y - 28 && g.y < p.y) {
                g.dead = true;
                hurtPlayer(i, (g.kind == 1 ? 14 : g.kind == 2 ? 11 : 9) + wave * 0.4f);
                fxGlobPop(g.x, g.y, g.kind, false);
            }
        }
        if (g.dead) continue;
        if ((g.kind != 1 && g.y > LANE_BOT + 4) || g.x > VW + 10 || g.y > VH + 10) {
            g.dead = true;
            fxGlobPop(g.x, std::min(g.y, LANE_BOT + 4.f), g.kind, false);
        }
    }
    globs.erase(std::remove_if(globs.begin(), globs.end(), [](const Glob& g) { return g.dead; }), globs.end());

    // Telegraphed fire pillars
    for (auto& h : hazards) {
        h.t += dt;
        if (h.done || h.t < h.dur) continue;
        h.done = true;
        fxPillar(h.x, h.y, h.r);
        for (int i = 0; i < MAX_SLOTS; i++) {
            const Player& p = players[i];
            if (p.alive() && std::hypot(p.x - h.x, (p.y - h.y) * 1.5f) < h.r + 3) hurtPlayer(i, h.dmg);
        }
    }
    hazards.erase(std::remove_if(hazards.begin(), hazards.end(), [](const Hazard& h) { return h.done && h.t > h.dur + 0.5f; }), hazards.end());
}
