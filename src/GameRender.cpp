// Rendering: world layers, dynamic lighting, emissive pass and the in-game HUD.
#include "Defs.hpp"
#include "Net.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>

using namespace util;

namespace {

void glow(float x, float y, float r, uint32_t col, float a) {
    gfx::tex(A.light, x - r, y - r, r * 2, r * 2, argb(col, uint32_t(clampf(a, 0, 1) * 255)), SDL_BLENDMODE_ADD);
}

void fogLayer(float y, float off, uint32_t alpha, uint32_t col) {
    float x0 = -std::fmod(off, float(A.fogW));
    for (float x = x0 - A.fogW; x < VW; x += A.fogW) gfx::tex(A.fog, x, y, float(A.fogW), float(A.fogH), argb(col, alpha));
}

std::string num(int v) { return std::to_string(v); }

float flick(float t, float seed, float amount) { return 1 - amount * (0.5f + 0.5f * std::sin(t * 13 + seed) * std::sin(t * 7.3f + seed * 2)); }

} // namespace

// ============================================================ frame

void Game::render() {
    // Persistent ground decals (blood, scorch marks).
    SDL_SetRenderTarget(R, decalTex);
    if (decalClear) {
        SDL_SetRenderDrawColor(R, 0, 0, 0, 0);
        SDL_RenderClear(R);
        decalClear = false;
    }
    for (auto& d : decals) {
        int n = int(d.r * d.r * 1.6f) + 2;
        for (int k = 0; k < n; k++) {
            float a = rnd(0, 2 * PI), rr = rnd(0, d.r);
            gfx::fillRect(d.x + std::cos(a) * rr * 1.5f, d.y + std::sin(a) * rr * 0.5f, 1, 1, argb(d.col, 210));
        }
    }
    decals.clear();

    buildLightmap();
    SDL_SetRenderTarget(R, sceneTex);
    drawBackground();
    gfx::tex(lightTex, 0, 0, VW, VH, 0xFFFFFFFF, SDL_BLENDMODE_MOD);
    drawBackgroundEmissive();
    SDL_SetRenderTarget(R, entityTex);
    SDL_SetRenderDrawColor(R, 0, 0, 0, 0);
    SDL_RenderClear(R);
    drawEntities();
    gfx::tex(lightTex, 0, 0, VW, VH, 0xFFFFFFFF, SDL_BLENDMODE_MOD);
    SDL_SetRenderTarget(R, sceneTex);
    gfx::tex(entityTex, 0, 0, VW, VH, 0xFFFFFFFF, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    drawEmissive();
    gfx::tex(A.vignette, 0, 0, VW, VH);
    bool inRun = mode == Mode::Play || mode == Mode::Shop;
    if (inRun && me().hurtFlash > 0) gfx::tex(A.vignette, 0, 0, VW, VH, argb(0xff2020, uint32_t(me().hurtFlash * 400)));
    if (mode == Mode::Play && !dying && me().alive() && me().hp < me().maxHp * 0.3f)
        gfx::tex(A.vignette, 0, 0, VW, VH, argb(0xc01020, uint32_t(60 + 50 * std::sin(time * 6))));
    if (mode == Mode::Play && (me().down || me().dead)) gfx::fillRect(0, 0, VW, VH, argb(0x200008, 90));

    switch (mode) {
    case Mode::Title: drawTitle(); break;
    case Mode::Lobby: drawLobby(); break;
    case Mode::Play: drawHud(); break;
    case Mode::Shop: drawShop(); break;
    case Mode::GameOver: drawGameOver(); break;
    }
    if (paused) drawPaused();
    if (showScores && (mode == Mode::Play || mode == Mode::Shop)) drawScoreboard();
    if (online()) drawChat();
    drawCursor();
    clickPending = false;
    menuKey = 0;

    SDL_SetRenderTarget(R, nullptr);
    SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
    SDL_RenderClear(R);
    float ox, oy, s;
    computeViewport(ox, oy, s);
    float shx = 0, shy = 0;
    if (shake > 0.3f && !(paused && !online())) {
        shx = std::round(rnd(-shake, shake) * 0.6f);
        shy = std::round(rnd(-shake, shake) * 0.6f);
    }
    SDL_FRect dst{ox + shx * s, oy + shy * s, VW * s, VH * s};
    SDL_RenderTexture(R, sceneTex, nullptr, &dst);
    SDL_RenderPresent(R);
}

// ============================================================ world

const Sprite& Game::enemySprite(const Enemy& e) const {
    int f = int(e.anim) & 1;
    switch (e.t) {
    case E_RAT: return A.rat[f];
    case E_RATLING: return A.ratling[f];
    case E_PLAGUERAT: return A.plagueRat[f];
    case E_GIANTRAT: return A.giantRat[f];
    case E_RATKING: return A.kingRat[f];
    case E_CROW: return A.crow[f];
    case E_BLOATER: return A.bloater[f];
    case E_ABOM: return A.abom[f];
    case E_SPITTER: return A.spitter[f];
    case E_BRUTE: return A.brute[f];
    case E_GHOUL: return e.z > 1 ? A.ghoulLeap : A.ghoul[f];
    case E_BAT: return A.bat[f];
    case E_KNIGHT: return A.knight[f];
    case E_CULTIST: return A.cultist[f];
    case E_BISHOP: return A.bishop[f];
    case E_HORSEMAN: return e.state == 3 ? A.horsemanRear : A.horseman[f];
    default: return A.rat[f];
    }
}

void Game::drawEnemy(const Enemy& e, bool flashPass) {
    const EnemyDef& d = EDEF[e.t];
    const Sprite& s = enemySprite(e);
    if (!s.tex) return;
    float sc = d.scale;
    bool flip = d.fly && e.vx < 0 && e.t != E_BISHOP;
    float x = e.x + e.lunge * 3 * (d.fly ? 0 : 1);
    if (e.trapT > 0 || e.stun > 0) x += std::sin(time * 50) * 0.6f;
    float left = x - s.w * sc / 2;
    float top = d.fly ? e.y - s.h * sc / 2 : e.y - s.h * sc - e.z;
    if (flashPass) {
        if (e.flash > 0) gfx::draw(s, left, top, flip, sc, true, argb(0xffffff, isBoss(e.t) ? 120 : 170));
        else if (e.burn > 0) gfx::draw(s, left, top, flip, sc, true, argb(0xff7020, uint32_t(70 + 40 * std::sin(time * 20))));
        else if (e.gas > 0) gfx::draw(s, left, top, flip, sc, true, argb(0x8aff40, uint32_t(40 + 25 * std::sin(time * 9))));
        else if (e.t == E_HORSEMAN && e.state == 3) gfx::draw(s, left, top, flip, sc, true, argb(0xffe080, uint32_t(50 + 40 * std::sin(time * 14))));
        for (const GlowPx& g : s.glow) {
            int gx = flip ? s.w - 1 - g.x : g.x;
            float px = std::floor(left) + gx * sc, py = std::floor(top) + g.y * sc;
            float pulse = g.strong ? 1.f : 0.6f + 0.4f * std::sin(time * 4 + g.x + e.id);
            gfx::fillRect(px, py, sc, sc, argb(g.col, uint32_t(255 * pulse)));
            glow(px + sc / 2, py + sc / 2, (g.strong ? 4.f : 3.f) * sc, g.col, (g.strong ? 0.55f : 0.3f) * pulse);
        }
        if (e.mark > 0) {
            float bob = std::sin(time * 6) * 1.5f;
            gfx::draw(A.ping, std::floor(e.x - A.ping.w / 2.f), std::floor(top - A.ping.h - 4 + bob), false, 1, false, argb(0xff5040));
        }
        if (e.t == E_HORSEMAN && e.state == 3)
            for (int k = 0; k < 3; k++) {
                float a = time * 4 + k * 2.1f;
                gfx::draw(A.star, e.x + std::cos(a) * 14 - 2, top + 4 + std::sin(a) * 4, false, 1, false, argb(0xffe080));
            }
        return;
    }
    gfx::draw(s, left, top, flip, sc);
    if (e.t == E_RATKING) {
        float cs = 2;
        gfx::draw(A.crown, std::floor(left) + 10 * sc - A.crown.w * cs / 2, std::floor(top) + 2 * sc - A.crown.h * cs + 2, false, cs);
    }
    bool showBar = !isBoss(e.t) && (d.scale >= 2 || e.t == E_BRUTE || e.t == E_BLOATER || e.t == E_SPITTER || e.t == E_KNIGHT ||
                                    e.t == E_CULTIST || e.t == E_GHOUL) &&
                   e.hp < e.maxHp;
    if (showBar) {
        SDL_FRect b = enemyBox(e);
        bar(e.x - 8, b.y - 5, 16, 2, e.hp / e.maxHp, 0xd04040, 0x2a0a10);
    }
}

void Game::drawDoctor(float x, float y, float scale, int skin, int weapon, float ang, bool left, bool locked, float rc, float deathRot) {
    int f = int(time * 1.7f) & 1;
    const Sprite& b = A.doctor[skin][f];
    uint32_t tint = locked ? 0xFF303038 : 0xFFFFFFFF;
    if (deathRot != 0) {
        gfx::drawRotated(b, x, y, b.w / 2.f, float(b.h), deathRot, false, scale, tint);
        return;
    }
    gfx::draw(b, x - b.w * scale / 2, y - b.h * scale, !left, scale, false, tint);
    float lx = left ? DOC_SHOULDER_X : b.w - DOC_SHOULDER_X;
    float sx = x - b.w * scale / 2 + lx * scale - std::cos(ang) * rc * scale;
    float sy = y - b.h * scale + (DOC_SHOULDER_Y + f) * scale - std::sin(ang) * rc * scale;
    const ArmGfx& a = A.arm[skin][weapon];
    gfx::drawRotated(a.spr, sx, sy, a.px, a.py, ang, left, scale, tint);
}

void Game::drawPlayer(const Player& p, int slot) {
    bool local = slot == localSlot;
    if (p.down || p.dead) {
        float rot = std::min(1.f, p.downT * 2.5f) * PI / 2 * (p.faceLeft ? 1 : -1);
        uint32_t c = p.dead ? 0x6a6070 : 0xffffff;
        const Sprite& b = A.doctor[p.skin][0];
        gfx::drawRotated(b, p.x, p.y, b.w / 2.f, float(b.h), rot, false, 1, argb(c, p.dead ? 160 : 255));
        if (!p.dead && activeCount() > 1) {
            // Revive beacon
            float bob = std::sin(time * 5) * 1.5f;
            gfx::draw(A.revive, p.x - 2, p.y - 26 + bob);
            glow(p.x, p.y - 24 + bob, 10, 0xff4040, 0.4f + 0.3f * std::sin(time * 8));
            bar(p.x - 10, p.y - 18, 20, 2, 1 - p.downT / 30.f, 0xd02a3a, 0x2a0a10);
            if (p.reviveT > 0) bar(p.x - 10, p.y - 14, 20, 2, p.reviveT, 0x8aff8a, 0x10200c);
        }
        return;
    }
    if (p.invulnT > 0 && int(time * 16) % 2 == 0) return;
    float walkBob = (mode == Mode::Play || mode == Mode::Shop) && std::fmod(p.walk, 2.f) > 1 ? -1.f : 0.f;
    drawDoctor(p.x, p.y + walkBob, 1, p.skin, p.cur, p.aimAng, p.faceLeft, false, p.recoil, 0);
    int f = int(time * 1.7f) & 1;
    float lx = p.faceLeft ? 7 : 18 - 9;
    gfx::fillRect(p.x - 9 + lx, p.y + walkBob - 28 + 8 + f, 2, 2, argb(SKINS[p.skin].eye));
    glow(p.x - 9 + lx + 1, p.y + walkBob - 28 + 9 + f, 6, SKINS[p.skin].eye, 0.8f);
    if (!local && activeCount() > 1) font::draw(p.name, p.x, p.y - 38, argb(SLOT_COL[slot], 220), 1, 1, true);
    if (p.muzzleT > 0 && mode == Mode::Play) {
        float x, y;
        muzzle(p, x, y);
        int k = WEAPONS[p.cur].kind;
        uint32_t mc = k == K_LIGHTNING ? 0x8ad8ff : (k == K_GAS ? 0x9aff5a : 0xffc060);
        if (k != K_LIGHTNING)
            gfx::drawRotated(A.muzzle, x, y, 0, 3.5f, p.aimAng, false, k == K_FLAME || k == K_GAS ? 0.7f : 1.f, 0xFFFFFFFF, SDL_BLENDMODE_ADD);
        glow(x, y, 18, mc, 0.8f);
    }
}

void Game::drawStageBackground(int s, float alpha) {
    const StageArt& st = A.stage[s];
    uint32_t tint = argb(0xffffff, uint32_t(alpha * 255));
    gfx::tex(st.sky, 0, 0, VW, VH, tint);
    for (auto& star : st.stars) {
        float a = 0.5f + 0.5f * std::sin(time * 2.3f + star.phase * 3);
        gfx::fillRect(star.x, star.y, 1, 1, argb(star.col, uint32_t(a * 255 * alpha)));
    }
    for (int i = 0; i < 3; i++) gfx::draw(A.cloud[i], cloudX[i], cloudY[i], false, 1, false, tint);
    gfx::tex(st.town, 0, 0, VW, VH, tint);
    gfx::tex(st.ground, 0, GROUND_Y, VW, VH - GROUND_Y, tint);
}

void Game::drawBackground() {
    int s = mode == Mode::Title || mode == Mode::Lobby ? titleStage : stage;
    drawStageBackground(s, 1);
    gfx::tex(A.clinic, 334, 100, 50, 94);
    gfx::fillRect(318, 117, 16, 1, argb(0x1a1418));
    float sa = std::sin(time * 1.3f) * 0.10f + (raining ? std::sin(time * 3.1f) * 0.07f : 0);
    gfx::drawRotated(A.sign, 325, 118, A.sign.w / 2.f, 1, sa, false);
    gfx::fillRect(334, 144, 2, 1, argb(0x1a1418));
    gfx::drawFeet(A.lantern, 334, 152);
    gfx::tex(decalTex, 0, 0, VW, VH);
    fogLayer(146, fogX, 70, STAGES[s].fog);
}

void Game::drawEntities() {
    bool inRun = mode == Mode::Play || mode == Mode::Shop || mode == Mode::GameOver;
    auto shadow = [&](float x, float y, float w, uint32_t a) { gfx::tex(A.shadow, x - w / 2, y - 3, w, 6, argb(0xffffff, a)); };
    for (auto& e : enemies) {
        const EnemyDef& d = EDEF[e.t];
        if (d.fly) shadow(e.x, e.t == E_BISHOP ? 186.f : LANE_BOT - 4, e.t == E_BISHOP ? 30.f : 8.f, 60);
        else shadow(e.x, e.y, (d.w * d.scale + 4) * (e.z > 0 ? 0.7f : 1.f), uint32_t(200 - std::min(120.f, e.z * 4)));
    }
    for (auto& c : critters) shadow(c.x, c.y, 14, 200);
    if (inRun)
        for (auto& p : players)
            if (p.active) shadow(p.x, p.y, p.down ? 26.f : 20.f, 220);
    for (auto& f : flaskList) shadow(lerp(f.x0, f.x1, f.t / f.dur), lerp(f.y0 + 8, f.y1, f.t / f.dur), 6, 120);

    // Plague gas sits low on the street.
    for (auto& g : gases) {
        float a = clampf(g.life, 0, 1);
        for (int k = 0; k < 5; k++) {
            float ox = std::sin(time * 0.7f + k * 1.7f + g.x) * g.r * 0.6f, oy = std::cos(time * 0.9f + k * 2.3f) * 4;
            float sz = g.r * (1.1f + 0.2f * std::sin(time + k));
            gfx::tex(A.puff, g.x + ox - sz / 2, g.y + oy - sz * 0.35f - 4, sz, sz * 0.6f, argb(k % 2 ? 0x6a9a3a : 0x8ab84a, uint32_t(a * 70)));
        }
    }

    struct DI { float y; int kind, idx; };
    std::vector<DI> list;
    list.reserve(enemies.size() + 16);
    for (size_t i = 0; i < enemies.size(); i++)
        if (!EDEF[enemies[i].t].fly) list.push_back({enemies[i].y, 0, int(i)});
    for (int i = 0; i < STAKES; i++) list.push_back({LANE_TOP + 2 + i * 4.f, 1, i});
    for (size_t i = 0; i < trapList.size(); i++) list.push_back({trapList[i].y - 0.5f, 3, int(i)});
    for (size_t i = 0; i < critters.size(); i++) list.push_back({critters[i].y, 4, int(i)});
    std::stable_sort(list.begin(), list.end(), [](const DI& a, const DI& b) { return a.y < b.y; });

    const int order[STAKES] = {4, 1, 7, 2, 6, 0, 8, 3, 5};
    float frac = barrMax > 0 ? barrHp / barrMax : 0;
    int broken = barrHp <= 0 ? STAKES : int((1 - frac) * 9.99f);
    auto stakeBroken = [&](int i) {
        for (int k = 0; k < broken && k < STAKES; k++)
            if (order[k] == i) return true;
        return false;
    };
    auto stakeX = [](int i) { return BARR_X - 4 + i * 1.f; };

    for (auto& it : list) {
        switch (it.kind) {
        case 0: drawEnemy(enemies[it.idx], false); break;
        case 1: {
            int i = it.idx;
            bool br = stakeBroken(i);
            const Sprite& s = br ? A.stakeBroken[barrTier] : A.stake[barrTier];
            float y = LANE_TOP + 2 + i * 4.f;
            if (!br && i % 2 == 0) gfx::draw(A.spike[barrTier], stakeX(i) - 14, y - 19);
            gfx::drawFeet(s, stakeX(i), y);
            if (!br && i % 2 == 1) gfx::draw(A.spike[barrTier], stakeX(i) - 14, y - 11);
            if (i + 1 < STAKES && !br && !stakeBroken(i + 1)) {
                uint32_t rope = barrTier == 2 ? argb(0x3a3d45) : argb(0x4a3420);
                gfx::line(stakeX(i), y - 15, stakeX(i + 1), y - 10, rope);
                gfx::line(stakeX(i), y - 7, stakeX(i + 1), y - 2, rope);
            }
            break;
        }
        case 3: {
            const Trap& t = trapList[it.idx];
            const Sprite& s = t.shut ? A.trapShut : A.trapOpen;
            uint32_t a = t.shut ? uint32_t(255 * clampf(3.5f - t.t, 0, 1)) : 255;
            gfx::drawFeet(s, t.x, t.y + 1, false, 1, false, argb(0xffffff, a));
            break;
        }
        case 4: {
            const Critter& c = critters[it.idx];
            gfx::drawFeet(A.rat[int(c.anim) & 1], c.x, c.y);
            break;
        }
        }
    }

    for (auto& p : parts) {
        if (p.kind == 3 || p.kind == 4 || p.kind == 6) continue;
        float a = clampf(p.life / std::min(p.max, 0.6f), 0, 1);
        if (p.kind == 1) {
            float u = 1 - p.life / p.max;
            float sz = p.size * (1 + u * 1.5f);
            gfx::tex(A.puff, p.x - sz / 2, p.y - sz / 2, sz, sz, argb(p.col, uint32_t(a * 110)));
        } else {
            gfx::fillRect(p.x, p.y, p.size, p.size, argb(p.col, uint32_t(a * 255)));
        }
    }

    for (auto& e : enemies)
        if (EDEF[e.t].fly) drawEnemy(e, false);

    for (auto& f : flaskList) {
        float u = f.t / f.dur;
        float x = lerp(f.x0, f.x1, u), y = lerp(f.y0, f.y1, u) - std::sin(PI * u) * 34;
        const Sprite& s = f.kind == 0 ? A.flask : A.gasCan;
        gfx::drawRotated(s, x, y, s.w / 2.f, s.h / 2.f, f.t * 14, false);
    }
    for (auto& b : bullets)
        if (b.kind == K_SAW) gfx::drawRotated(A.saw, b.x, b.y - 3, A.saw.w / 2.f, A.saw.h / 2.f, b.spin, false);

    int s = mode == Mode::Title || mode == Mode::Lobby ? titleStage : stage;
    int kind = STAGES[s].weather;
    for (auto& d : weather) {
        if (kind == 0) gfx::line(d.x, d.y, d.x - 1, d.y - 4, argb(0x9ab0d8, 120));
        else if (kind == 3) gfx::fillRect(d.x, d.y, (int(d.ph * 10) % 3 == 0) ? 2 : 1, 1, argb(0xf0f4ff, 200));
    }
    fogLayer(184, fogX * 1.7f + 40, 55, STAGES[s].fog);
}

// ============================================================ lighting

void Game::buildLightmap() {
    SDL_SetRenderTarget(R, lightTex);
    int s = mode == Mode::Title || mode == Mode::Lobby ? titleStage : stage;
    float fl = 0;
    if (flashT > 0) fl = (flashT > 0.28f || (flashT < 0.2f && flashT > 0.12f)) ? 1.f : 0.35f;
    uint32_t amb = mixRGB(STAGES[s].ambient, 0xd8d8ff, fl * 0.85f);
    SDL_SetRenderDrawColor(R, (amb >> 16) & 255, (amb >> 8) & 255, amb & 255, 255);
    SDL_RenderClear(R);

    auto L = [&](float x, float y, float r, uint32_t col, float a) {
        gfx::tex(A.light, x - r, y - r, r * 2, r * 2, argb(col, uint32_t(clampf(a, 0, 1) * 255)), SDL_BLENDMODE_ADD);
    };
    for (auto& l : A.stage[s].lights) L(l.x, l.y, l.r, l.col, l.a * flick(time, l.x * 0.37f + l.y, l.flicker));
    float lf = 0.85f + 0.15f * std::sin(time * 11) * std::sin(time * 5.1f);
    L(334, 150, 62, 0xffa040, 0.95f * lf);
    L(345, 162, 34, 0xffa050, 0.5f);
    L(362, 137, 30, 0xffa050, 0.4f);
    if (mode == Mode::Play || mode == Mode::Shop || mode == Mode::GameOver) {
        for (auto& p : players) {
            if (!p.active) continue;
            if (!p.down && !p.dead) {
                float ex = p.x - 9 + (p.faceLeft ? DOC_EYE_X : 18 - DOC_EYE_X);
                L(ex, p.y - 28 + DOC_EYE_Y, 12, SKINS[p.skin].eye, 0.6f);
            }
            L(p.x - 6, p.y - 14, 48, 0xffb070, 0.4f);
            if (p.muzzleT > 0) {
                float x, y;
                muzzle(p, x, y);
                int k = WEAPONS[p.cur].kind;
                L(x, y, k == K_FLAME ? 50.f : 95.f, k == K_LIGHTNING ? 0x8ad8ff : 0xffc070, 1);
            }
        }
    }
    for (auto& b : bullets) L(b.x, b.y, b.kind == K_FLAME ? 22.f : 12.f, b.col, b.kind == K_FLAME ? 0.22f : 0.45f);
    for (auto& f : fires) L(f.x, f.y - 6, 44, 0xff8030, 0.7f * clampf(f.life, 0, 1));
    for (auto& g : gases) L(g.x, g.y - 4, g.r * 1.6f, 0x8aff40, 0.22f * clampf(g.life, 0, 1));
    for (auto& e : enemies) {
        SDL_FRect bx = enemyBox(e);
        if (e.burn > 0) L(bx.x + bx.w / 2, bx.y + bx.h / 2, 32, 0xff8030, 0.6f);
        if (e.t == E_BLOATER || e.t == E_ABOM) L(bx.x + bx.w / 2, bx.y + bx.h / 2, bx.w * 1.6f, 0x9aff40, 0.25f);
        if (e.t == E_PLAGUERAT) L(e.x, e.y - 4, 12, 0x9aff40, 0.25f);
        if (e.t == E_CULTIST) L(e.x + 5, e.y - 14, 18, 0x8aff6a, 0.35f);
        if (e.t == E_BISHOP) L(e.x, e.y, 80, 0xffb060, 0.45f);
        if (e.t == E_HORSEMAN) L(e.x, e.y - 30, 50, 0xa0c0ff, 0.3f);
    }
    for (auto& h : hazards)
        if (!h.done) L(h.x, h.y - 4, h.r * 2.2f, 0xff5020, 0.25f + 0.5f * clampf(h.t / h.dur, 0, 1));
    for (auto& t : tlights) L(t.x, t.y, t.r * (0.6f + 0.4f * t.life / t.max), t.col, t.life / t.max);
    for (auto& g : globs) L(g.x, g.y, 16, g.kind == 2 ? 0xff8030 : (g.kind == 1 ? 0xb0c0ff : 0x9aff40), 0.55f);
    for (auto& p : pickups) L(p.x, p.y, 8, 0xffd040, 0.35f);
    for (auto& f : flaskList) {
        float u = f.t / f.dur;
        L(lerp(f.x0, f.x1, u), lerp(f.y0, f.y1, u) - std::sin(PI * u) * 34, 18, f.kind == 0 ? 0xff8030 : 0x8aff40, 0.5f);
    }
    int fireLights = 0;
    for (auto& p : parts)
        if (p.kind == 4 && (fireLights++ % 3) == 0) L(p.x, p.y, p.size * 2.5f, 0xff7020, 0.3f * p.life / p.max);
    if (STAGES[s].weather == 2)
        for (size_t i = 0; i < weather.size(); i += 4) L(weather[i].x, weather[i].y, 6, 0xff7020, 0.3f);
}

void Game::drawBackgroundEmissive() {
    int s = mode == Mode::Title || mode == Mode::Lobby ? titleStage : stage;
    for (auto& w : A.stage[s].windows) {
        float f = 0.8f + 0.2f * std::sin(time * 1.7f + w.phase) * std::sin(time * 3.1f + w.phase * 2);
        uint32_t c = mixRGB(0x000000, w.col, f);
        gfx::fillRect(w.x, w.y, w.w, w.h, argb(c, 235));
        if (w.w >= 5 && w.h >= 5) {
            gfx::fillRect(w.x + std::floor(w.w / 2), w.y, 1, w.h, argb(0x2a1c14));
            gfx::fillRect(w.x, w.y + std::floor(w.h / 2), w.w, 1, argb(0x2a1c14));
        }
        glow(w.x + w.w / 2, w.y + w.h / 2, std::max(w.w, w.h) * 1.8f + 2, w.col, 0.25f * f);
    }
    for (auto& g : A.stage[s].glows) glow(g.x, g.y, g.r, g.col, g.a * flick(time, g.x * 0.53f + g.y, g.flicker));
    // Clinic windows (shared by every stage) are part of the clinic texture's light.
    glow(334, 149, 12, 0xffa040, 0.7f);
    glow(325, 124, 7, 0x6aff7a, 0.25f);

    if (flashT > 0.2f && bolt.size() > 1) {
        for (size_t i = 1; i < bolt.size(); i++) {
            gfx::line(bolt[i - 1].first, bolt[i - 1].second, bolt[i].first, bolt[i].second, argb(0xf0f0ff));
            gfx::line(bolt[i - 1].first + 1, bolt[i - 1].second, bolt[i].first + 1, bolt[i].second, argb(0x8a8aff, 120));
        }
    }
    // Weather that glows: marsh motes and embers.
    int kind = STAGES[s].weather;
    for (auto& d : weather) {
        if (kind == 1) {
            float a = std::sin(PI * clampf(d.ph / 9, 0, 1)) * (0.6f + 0.4f * std::sin(d.ph * 5));
            gfx::fillRect(d.x, d.y, 1, 1, argb(0xc8ff8a, uint32_t(a * 255)));
            glow(d.x, d.y, 4, 0x9aff6a, a * 0.5f);
        } else if (kind == 2) {
            float a = 0.6f + 0.4f * std::sin(d.ph * 9);
            gfx::fillRect(d.x, d.y, 1, 1, argb(0xffb050, uint32_t(a * 255)));
        }
    }
}

void Game::drawEmissive() {
    bool inRun = mode == Mode::Play || mode == Mode::Shop || mode == Mode::GameOver;
    // Doctors are drawn unlit so they always read clearly against the night.
    if (inRun) {
        // Telegraphed hellfire circles go under the doctors.
        for (auto& h : hazards) {
            if (h.done) continue;
            float u = clampf(h.t / h.dur, 0, 1);
            int n = 28;
            uint32_t c = argb(u > 0.75f && int(time * 20) % 2 ? 0xffe080 : 0xff4020, uint32_t(120 + 120 * u));
            for (int k = 0; k < n; k++) {
                float a0 = k * 2 * PI / n, a1 = (k + 1) * 2 * PI / n;
                gfx::line(h.x + std::cos(a0) * h.r, h.y + std::sin(a0) * h.r * 0.45f, h.x + std::cos(a1) * h.r,
                          h.y + std::sin(a1) * h.r * 0.45f, c);
            }
            float ir = h.r * u;
            gfx::tex(A.puff, h.x - ir, h.y - ir * 0.45f, ir * 2, ir * 0.9f, argb(0xff3010, uint32_t(90 * u)), SDL_BLENDMODE_ADD);
        }
        std::vector<int> order;
        for (int i = 0; i < MAX_SLOTS; i++)
            if (players[i].active) order.push_back(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return players[a].y < players[b].y; });
        for (int i : order) drawPlayer(players[i], i);
    }

    for (auto& e : enemies) drawEnemy(e, true);

    for (auto& b : bullets) {
        if (b.kind == K_FLAME) {
            float u = 1 - b.life / b.maxLife;
            float sz = 4 + u * 14;
            uint32_t c = u < 0.3f ? 0xfff0a0 : (u < 0.6f ? 0xffa030 : 0xc04010);
            gfx::tex(A.puff, b.x - sz / 2, b.y - sz / 2, sz, sz, argb(c, uint32_t((1 - u) * 200)), SDL_BLENDMODE_ADD);
        } else if (b.kind == K_SAW) {
            glow(b.x, b.y - 3, 7, 0xfff0d0, 0.3f);
        } else if (b.kind == K_LEECH) {
            gfx::drawRotated(A.leech, b.x, b.y, A.leech.w / 2.f, A.leech.h / 2.f, std::atan2(b.vy, b.vx), false);
            glow(b.x, b.y, 5, b.col, 0.4f);
        } else {
            float tl = b.weapon == 2 ? 0.022f : (b.weapon == 5 ? 0.04f : 0.016f);
            gfx::line(b.x - b.vx * tl, b.y - b.vy * tl, b.x, b.y, argb(b.col, 200));
            gfx::fillRect(b.x, b.y, 1, 1, argb(0xffffff));
            glow(b.x, b.y, b.perfect ? 9.f : 6.f, b.col, b.perfect ? 0.8f : 0.5f);
        }
    }
    for (auto& a : arcs) {
        float al = clampf(a.life / 0.16f, 0, 1);
        for (size_t i = 1; i < a.pts.size(); i++) {
            gfx::line(a.pts[i - 1].first, a.pts[i - 1].second, a.pts[i].first, a.pts[i].second, argb(0xffffff, uint32_t(255 * al)));
            gfx::line(a.pts[i - 1].first, a.pts[i - 1].second + 1, a.pts[i].first, a.pts[i].second + 1, argb(a.col, uint32_t(160 * al)));
        }
        for (size_t i = 0; i < a.pts.size(); i += 3) glow(a.pts[i].first, a.pts[i].second, 8, a.col, 0.4f * al);
    }
    for (auto& p : parts) {
        float a = clampf(p.life / p.max, 0, 1);
        if (p.kind == 3) gfx::line(p.x - p.vx * 0.025f, p.y - p.vy * 0.025f, p.x, p.y, argb(p.col, uint32_t(a * 255)));
        else if (p.kind == 4) {
            float u = 1 - a, sz = p.size * (1 + u);
            uint32_t c = u < 0.35f ? 0xffe0a0 : (u < 0.7f ? p.col : 0x902808);
            gfx::tex(A.puff, p.x - sz / 2, p.y - sz / 2, sz, sz, argb(c, uint32_t(a * 220)), SDL_BLENDMODE_ADD);
        } else if (p.kind == 6) {
            gfx::fillRect(p.x, p.y, 1, 1, argb(p.col, uint32_t(a * 255)));
            glow(p.x, p.y, 3, p.col, a * 0.5f);
        }
    }
    for (auto& g : globs) {
        if (g.kind == 1) {
            gfx::drawRotated(A.scythe, g.x, g.y, A.scythe.w / 2.f, A.scythe.h / 2.f, time * 14, false);
            glow(g.x, g.y, 8, 0xb0c0ff, 0.4f);
        } else if (g.kind == 2) {
            gfx::tex(A.puff, g.x - 4, g.y - 4, 8, 8, argb(0xffd070, 230), SDL_BLENDMODE_ADD);
            glow(g.x, g.y, 9, 0xff6020, 0.8f);
        } else {
            gfx::draw(A.glob, g.x - 3, g.y - 3);
            glow(g.x, g.y, 7, 0x9aff40, 0.6f);
        }
    }
    for (auto& p : pickups) {
        gfx::draw(A.coin[(int(time * 8 + p.x) & 3) == 0 ? 1 : 0], p.x - 3, p.y - 3);
        if ((int(time * 10 + p.x) % 7) == 0) gfx::fillRect(p.x + 1, p.y - 2, 1, 1, argb(0xffffff));
    }
    for (auto& pg : pings) {
        float a = clampf(pg.life, 0, 1);
        float bob = std::sin(time * 6) * 2;
        uint32_t c = SLOT_COL[pg.slot % MAX_SLOTS];
        gfx::draw(A.ping, pg.x - A.ping.w / 2.f, pg.y - A.ping.h - 2 + bob, false, 1, false, argb(c, uint32_t(255 * a)));
        float rr = 4 + (3 - pg.life) * 6;
        if (pg.life > 2) gfx::rect(pg.x - rr, pg.y - rr * 0.4f, rr * 2, rr * 0.8f, argb(c, uint32_t(200 * (pg.life - 2))));
        glow(pg.x, pg.y - 4, 10, c, 0.5f * a);
    }
}

// ============================================================ UI primitives

void Game::bar(float x, float y, float w, float h, float frac, uint32_t col, uint32_t back) {
    gfx::fillRect(x - 1, y - 1, w + 2, h + 2, argb(0x0d0a12));
    gfx::fillRect(x, y, w, h, argb(back));
    float fw = std::floor(w * clampf(frac, 0, 1));
    gfx::fillRect(x, y, fw, h, argb(col));
    if (h >= 3) gfx::fillRect(x, y, fw, 1, argb(mixRGB(col, 0xffffff, 0.35f)));
}

void Game::drawCursor() {
    const Player& p = me();
    if (mode == Mode::Play && !dying && !paused && !chatOpen && p.alive()) {
        float g = 2 + std::round(p.crossSpread);
        uint32_t c = p.reloading ? argb(0xa0a0a0) : argb(0xffffff);
        uint32_t d = argb(0x0d0a12, 200);
        float x = std::floor(mx), y = std::floor(my);
        for (int k = 0; k < 2; k++) {
            uint32_t cc = k == 0 ? d : c;
            float o = k == 0 ? 1 : 0;
            gfx::fillRect(x - g - 3 + o, y + o, 3, 1, cc);
            gfx::fillRect(x + g + 1 + o, y + o, 3, 1, cc);
            gfx::fillRect(x + o, y - g - 3 + o, 1, 3, cc);
            gfx::fillRect(x + o, y + g + 1 + o, 1, 3, cc);
        }
        gfx::fillRect(x, y, 1, 1, argb(0xff5040));
        if (p.reloading) {
            float u = p.reloadT / p.reloadDur;
            gfx::fillRect(x - 6, y + g + 6, 12, 2, d);
            gfx::fillRect(x - 6, y + g + 6, std::floor(12 * clampf(u, 0, 1)), 1, argb(0xffd860));
        }
    } else {
        gfx::draw(A.cursor, std::floor(mx) - 1, std::floor(my) - 1);
    }
}

// ============================================================ HUD

void Game::drawWeapons(float, float, float) {
    const Player& p = me();
    for (int i = 0; i < WEAPON_COUNT; i++) {
        SDL_FRect r = slotRect(i);
        bool sel = i == p.cur;
        gfx::fillRect(r.x, r.y, r.w, r.h, argb(sel ? 0x3a2a1a : 0x140f1a, 220));
        gfx::rect(r.x, r.y, r.w, r.h, argb(sel ? 0xffd860 : (p.owned[i] ? 0x6a5a4a : 0x2e2830)));
        const Sprite& ic = A.weaponIcon[i];
        SDL_Rect clip{0, 0, 0, 0};
        float ox, oy, s;
        (void)ox, (void)oy, (void)s;
        // Long guns are clipped to the slot so the bar stays tidy.
        clip = SDL_Rect{int(r.x + 1), int(r.y + 1), int(r.w - 2), int(r.h - 2)};
        SDL_SetRenderClipRect(R, &clip);
        float ix = ic.w <= r.w - 2 ? r.x + std::floor((r.w - ic.w) / 2) : r.x + r.w - 1 - ic.w;
        gfx::draw(ic, ix, r.y + std::floor((r.h - ic.h) / 2) + 1, false, 1, false, p.owned[i] ? 0xFFFFFFFF : argb(0x404048, 200));
        SDL_SetRenderClipRect(R, nullptr);
        font::draw(i == 9 ? "0" : num(i + 1), r.x + 2, r.y + 2, argb(sel ? 0xffd860 : 0x8a7a6a), 1, 0, false);
        if (!p.owned[i]) gfx::draw(A.lock, r.x + r.w - 8, r.y + r.h - 8);
    }
}

void Game::drawTeam() {
    // Colleague cards under our own bars.
    float y = 25;
    for (int i = 0; i < MAX_SLOTS; i++) {
        const Player& p = players[i];
        if (!p.active || i == localSlot) continue;
        gfx::fillRect(3, y - 1, 74, 9, argb(0x0d0a12, 140));
        gfx::fillRect(3, y - 1, 2, 9, argb(SLOT_COL[i]));
        font::draw(p.name, 7, y, argb(SLOT_COL[i]), 1, 0, false);
        if (p.dead) font::draw("BLED OUT", 76, y, argb(0x8a7070), 1, 2, false);
        else if (p.down) {
            if (int(time * 4) & 1) font::draw("DOWN " + num(int(std::ceil(30 - p.downT))), 76, y, argb(0xff4040), 1, 2, false);
        } else bar(50, y + 1, 26, 3, p.hp / p.maxHp, 0xd02a3a, 0x3a1018);
        y += 10;
    }
}

void Game::drawHud() {
    const Player& p = me();
    gfx::draw(A.heart, 4, 3);
    bar(15, 5, 64, 5, p.hp / p.maxHp, 0xd02a3a, 0x3a1018);
    font::draw(num(int(std::ceil(p.hp))) + "/" + num(int(p.maxHp)), 84, 5, argb(0xffc0c0));
    gfx::draw(A.plank, 4, 13);
    bar(15, 15, 64, 5, barrHp / barrMax, barrTier == 2 ? 0x9aa0aa : 0xc88a3a, 0x2a1a10);
    if (barrHp <= 0) {
        if (int(time * 4) & 1) font::draw("BROKEN!", 84, 15, argb(0xff4040));
    } else {
        font::draw(num(int(std::ceil(barrHp))), 84, 15, argb(0xe8c890));
    }
    if (activeCount() > 1) drawTeam();

    font::draw("WAVE " + num(wave), VW / 2, 4, argb(0xffd860), 1, 1);
    font::draw(STAGES[stage].name, VW / 2 + font::width("WAVE " + num(wave)) / 2 + 6, 4, argb(0x8a7a6a), 1, 0, false);
    bar(VW / 2 - 40, 12, 80, 2, waveTotal > 0 ? float(waveKilled) / waveTotal : 0, 0x9a7ad8, 0x1a1428);

    float comboY = 20;
    int bosses = 0;
    for (auto& e : enemies) {
        if (!isBoss(e.t) || e.dead) continue;
        float by = 19 + bosses * 15;
        font::draw(EDEF[e.t].name, VW / 2, by, argb(0xff8080), 1, 1, true);
        bar(VW / 2 - 70, by + 8, 140, 4, e.hp / e.maxHp, e.t == E_HORSEMAN && e.state == 3 ? 0xffd860 : 0xc02040, 0x2a0a10);
        comboY = by + 17;
        if (++bosses >= 2) break;
    }

    float cp = coinPulse > 0 ? 1 : 0;
    gfx::draw(A.coin[0], HUD_COIN_X - 3, HUD_COIN_Y - 3 - cp);
    font::draw(num(p.coins), VW - 6, 6, argb(coinPulse > 0 ? 0xffffff : 0xffd860), 1, 2);
    gfx::draw(A.skull, HUD_COIN_X - 3, 15);
    font::draw(num(p.kills), VW - 6, 16, argb(0xe8e0cc), 1, 2);
    if (online()) {
        int pg = role == Role::Client ? net::pingMs(net::hostId()) : -1;
        if (pg >= 0) font::draw(num(pg) + "MS", VW - 6, 26, argb(pg < 80 ? 0x6a8a6a : (pg < 160 ? 0xc8a050 : 0xc05050)), 1, 2);
    }

    if (p.combo >= 2) {
        int sc = p.combo >= 10 ? 2 : 1;
        uint32_t c = p.combo >= 20 ? 0xff60ff : (p.combo >= 10 ? 0xff9a30 : 0xffe060);
        float wob = std::sin(time * 18) * (p.combo >= 10 ? 1.f : 0.f);
        font::draw("X" + num(p.combo) + " COMBO", VW / 2, comboY + wob, argb(c), sc, 1, true);
        bar(VW / 2 - 20, comboY + 6 * sc + 2, 40, 1, p.comboT / 2.2f, c, 0x1a1428);
    }
    if (lastStand && p.alive()) font::draw("LAST STAND: +25% DAMAGE & RATE", VW / 2, VH - 38, argb(0xff9a30, uint32_t(180 + 60 * std::sin(time * 6))), 1, 1, true);

    drawWeapons(0, 0, 0);
    {
        const WeaponDef& d = WEAPONS[p.cur];
        float ay = VH - 29;
        int clip = clipSize(p, p.cur);
        if (d.kind == K_FLAME || clip > 16) {
            bar(6, ay + 1, 60, 3, float(p.ammo[p.cur]) / clip, d.kind == K_FLAME ? 0xff8a30 : d.col, 0x2a1408);
            font::draw(d.kind == K_FLAME ? "FUEL" : num(p.ammo[p.cur]), 70, ay, argb(0xffb070));
        } else {
            for (int k = 0; k < clip; k++) {
                bool full = k < p.ammo[p.cur];
                uint32_t c = full ? (p.blessedShots > 0 ? 0xfff080 : 0xd8b048) : 0x3a2e28;
                gfx::fillRect(6 + k * 4, ay, 2, 5, argb(0x0d0a12));
                gfx::fillRect(6 + k * 4, ay, 2, 4, argb(c));
                if (full) gfx::fillRect(6 + k * 4, ay, 2, 1, argb(0xfff4c0));
            }
        }
        float tx = 6 + std::min(clip, 16) * 4 + 4 + (clip > 16 || d.kind == K_FLAME ? 20 : 0);
        if (p.reloading) font::draw(p.jammed ? "JAMMED..." : "RELOADING - PRESS R!", tx, ay, argb(p.jammed ? 0xff6050 : 0xc8c0b0));
        else font::draw(d.name, tx, ay, argb(0xa89880));
        if (p.blessedShots > 0) font::draw("BLESSED X" + num(p.blessedShots), 6, ay - 8, argb(0xfff080));
    }

    struct Item { const Sprite* s; int n; const char* key; };
    const Item items[3] = {{&A.flask, p.flasks, "G"}, {&A.trapOpen, p.traps, "E"}, {&A.tonic, p.tonics, "Q"}};
    for (int k = 0; k < 3; k++) {
        float x = VW - 93 + k * 30, y = VH - 20;
        gfx::fillRect(x, y, 28, 16, argb(0x140f1a, 220));
        gfx::rect(x, y, 28, 16, argb(items[k].n > 0 ? 0x6a5a4a : 0x2e2830));
        const Sprite& s = *items[k].s;
        gfx::draw(s, x + 2, y + std::floor((16 - s.h) / 2), false, 1, false, items[k].n > 0 ? 0xFFFFFFFF : argb(0x505058));
        font::draw(num(items[k].n), x + 26, y + 9, argb(0xffffff), 1, 2);
        font::draw(items[k].key, x + 26, y + 2, argb(0x8a7a6a), 1, 2, false);
    }

    if (p.reloading && p.alive()) {
        float bx = p.x - 15, by = p.y - 44, bw = 30;
        float lo = p.skin == 5 ? 0.36f : 0.42f, hi = p.skin == 5 ? 0.68f : 0.60f;
        gfx::fillRect(bx - 1, by - 1, bw + 2, 6, argb(0x0d0a12));
        gfx::fillRect(bx, by, bw, 4, argb(0x2a2430));
        gfx::fillRect(bx + std::floor(bw * lo), by, std::ceil(bw * (hi - lo)), 4, argb(p.jammed ? 0x6a2020 : 0xc8a030));
        float u = clampf(p.reloadT / p.reloadDur, 0, 1);
        gfx::fillRect(bx + std::floor(bw * u), by - 1, 1, 6, argb(0xffffff));
    }

    // Context hints for the F key.
    if (p.alive()) {
        std::string hint;
        for (int i = 0; i < MAX_SLOTS && hint.empty(); i++) {
            const Player& q = players[i];
            if (i != localSlot && q.active && q.down && !q.dead && std::hypot(q.x - p.x, (q.y - p.y) * 1.4f) <= 20)
                hint = "HOLD F: REVIVE " + q.name;
        }
        if (hint.empty() && p.x < BARR_X + 26 && barrHp < barrMax) hint = p.coins > 0 ? "HOLD F: REPAIR STAKES (1 COIN / 2 HP)" : "NO COINS TO REPAIR";
        if (!hint.empty()) font::draw(hint, p.x - 30, p.y + 6 > VH - 30 ? p.y - 52 : p.y + 4, argb(0xfff0c0, uint32_t(200 + 55 * std::sin(time * 5))), 1, 1, true);
    } else if (p.down && !p.dead) {
        font::draw("YOU ARE DOWN", VW / 2, 84, argb(0xff4040), 2, 1, true);
        font::draw("BLEEDING OUT IN " + num(int(std::ceil(30 - p.downT))) + "S - A COLLEAGUE CAN REVIVE YOU", VW / 2, 98, argb(0xd8c0c0), 1, 1, true);
    } else if (p.dead && !dying) {
        font::draw("YOU BLED OUT", VW / 2, 84, argb(0xa08080), 2, 1, true);
        font::draw("YOU WILL RISE WHEN THE WAVE IS CLEARED", VW / 2, 98, argb(0xc8b8b8), 1, 1, true);
    }

    for (auto& pp : popups) {
        float a = clampf(pp.life / pp.max * 2.5f, 0, 1);
        font::draw(pp.s, pp.x, pp.y, argb(pp.col, uint32_t(a * 255)), pp.scale, 1);
    }
    if (bannerT > 0) {
        float u = 1 - bannerT / bannerMax;
        float a = std::min({1.f, u * 6, bannerT * 2.5f});
        float slide = (1 - std::min(1.f, u * 5)) * 30;
        float y = 62;
        gfx::fillRect(0, y - 8, VW, 34, argb(0x000000, uint32_t(110 * a)));
        int sc = font::width(bannerText, 3) > VW - 10 ? 2 : 3;
        font::draw(bannerText, VW / 2 - slide, y + (3 - sc) * 3, argb(bannerCol, uint32_t(255 * a)), sc, 1, true);
        if (!bannerSub.empty()) font::draw(bannerSub, VW / 2 + slide, y + 19, argb(0xd8d0c0, uint32_t(255 * a)), 1, 1);
    }
    float ty = 100;
    for (auto& t : toasts) {
        float a = clampf(t.life, 0, 1);
        font::draw(t.s, VW / 2, ty, argb(t.col, uint32_t(255 * a)), 1, 1, true);
        ty += 9;
    }
    if (wave == 1 && waveT < 14 && mode == Mode::Play) {
        float a = clampf(14 - waveT, 0, 1);
        uint32_t c = argb(0xc8c0b0, uint32_t(200 * a));
        font::draw("CLICK TO SHOOT - AIM AT HEADS FOR CRITS - WASD TO MOVE", VW / 2, 132, c, 1, 1);
        font::draw("PRESS R IN THE GOLD ZONE FOR A PERFECT RELOAD", VW / 2, 140, c, 1, 1);
        if (activeCount() > 1) font::draw("MMB/V PINGS A FOE: EVERYONE DEALS +15% TO IT", VW / 2, 148, c, 1, 1);
    }
}

void Game::drawScoreboard() {
    float w = 300, h = 22 + activeCount() * 12, x = VW / 2 - w / 2, y = 44;
    panel(x, y, w, h);
    const char* heads[] = {"DOCTOR", "KILLS", "DAMAGE", "COMBO", "REVIVES", "DOWNS", "PING"};
    const float cx[] = {x + 8, x + 110, x + 150, x + 192, x + 226, x + 258, x + 290};
    for (int k = 0; k < 7; k++) font::draw(heads[k], cx[k], y + 7, argb(0xc8a878), 1, k == 0 ? 0 : 2, false);
    float ry = y + 18;
    for (int i = 0; i < MAX_SLOTS; i++) {
        const Player& p = players[i];
        if (!p.active) continue;
        gfx::fillRect(x + 4, ry - 1, 2, 7, argb(SLOT_COL[i]));
        font::draw(p.name + (p.down ? " (DOWN)" : ""), cx[0], ry, argb(i == localSlot ? 0xffffff : SLOT_COL[i]), 1, 0, false);
        std::string vals[] = {num(p.kills), num(int(p.dmgDealt)), "X" + num(p.bestCombo), num(p.revives), num(p.downs),
                              p.ping >= 0 && online() ? num(p.ping) : "-"};
        for (int k = 0; k < 6; k++) font::draw(vals[k], cx[k + 1], ry, argb(0xe8e0cc), 1, 2, false);
        ry += 12;
    }
}

void Game::drawChat() {
    float y = VH - 48;
    if (chatOpen) {
        gfx::fillRect(4, y + 8, 220, 9, argb(0x0d0a12, 200));
        gfx::rect(4, y + 8, 220, 9, argb(0x8a6a3a));
        std::string t = "SAY: " + chatText + ((int(time * 3) & 1) ? "_" : "");
        font::draw(t, 7, y + 10, argb(0xffffff), 1, 0, false);
    }
    for (int i = int(chats.size()) - 1; i >= 0; i--) {
        const Chat& c = chats[i];
        float a = chatOpen ? 1.f : clampf(c.life, 0, 1);
        font::draw(c.s, 6, y, argb(c.col, uint32_t(230 * a)), 1, 0, false);
        y -= 8;
    }
}
