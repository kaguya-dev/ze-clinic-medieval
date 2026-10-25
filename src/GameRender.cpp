// Rendering: world layers, dynamic lighting, emissive pass, HUD and menus.
#include "Defs.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>

using namespace util;

namespace {

void glow(float x, float y, float r, uint32_t col, float a) {
    gfx::tex(A.light, x - r, y - r, r * 2, r * 2, argb(col, uint32_t(clampf(a, 0, 1) * 255)), SDL_BLENDMODE_ADD);
}

void fogLayer(float y, float off, uint32_t alpha) {
    float x0 = -std::fmod(off, float(A.fogW));
    for (float x = x0 - A.fogW; x < VW; x += A.fogW) gfx::tex(A.fog, x, y, float(A.fogW), float(A.fogH), argb(0xa8c0a0, alpha));
}

std::string num(int v) { return std::to_string(v); }

bool inside(float x, float y, float rx, float ry, float rw, float rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

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

    // Lit background, then its own light sources (windows, lamps) on top.
    buildLightmap();
    SDL_SetRenderTarget(R, sceneTex);
    drawBackground();
    gfx::tex(lightTex, 0, 0, VW, VH, 0xFFFFFFFF, SDL_BLENDMODE_MOD);
    drawBackgroundEmissive();
    // Entities go to a transparent premultiplied layer so they can be lit separately and still occlude
    // the background glow.
    SDL_SetRenderTarget(R, entityTex);
    SDL_SetRenderDrawColor(R, 0, 0, 0, 0);
    SDL_RenderClear(R);
    drawEntities();
    gfx::tex(lightTex, 0, 0, VW, VH, 0xFFFFFFFF, SDL_BLENDMODE_MOD);
    SDL_SetRenderTarget(R, sceneTex);
    gfx::tex(entityTex, 0, 0, VW, VH, 0xFFFFFFFF, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    drawEmissive();
    gfx::tex(A.vignette, 0, 0, VW, VH);
    if (hurtFlash > 0) gfx::tex(A.vignette, 0, 0, VW, VH, argb(0xff2020, uint32_t(hurtFlash * 400)));
    if (mode == Mode::Play && !dying && hp < maxHp * 0.3f)
        gfx::tex(A.vignette, 0, 0, VW, VH, argb(0xc01020, uint32_t(60 + 50 * std::sin(time * 6))));

    switch (mode) {
    case Mode::Title: drawTitle(); break;
    case Mode::Play: drawHud(); break;
    case Mode::Shop: drawShop(); break;
    case Mode::Paused:
        drawHud();
        drawPaused();
        break;
    case Mode::GameOver: drawGameOver(); break;
    }
    drawCursor();
    clickPending = false;

    SDL_SetRenderTarget(R, nullptr);
    SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
    SDL_RenderClear(R);
    float ox, oy, s;
    computeViewport(ox, oy, s);
    float shx = 0, shy = 0;
    if (shake > 0.3f && mode != Mode::Paused) {
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
    default: return A.rat[f];
    }
}

void Game::drawEnemy(const Enemy& e, bool flashPass) {
    const EnemyDef& d = EDEF[e.t];
    const Sprite& s = enemySprite(e);
    float sc = d.scale;
    bool flip = d.fly && e.vx < 0;
    float x = e.x + e.lunge * 3 * (d.fly ? 0 : 1);
    if (e.trapT > 0) x += std::sin(time * 50) * 0.6f;
    float left = d.fly ? x - s.w * sc / 2 : x - s.w * sc / 2;
    float top = d.fly ? e.y - s.h * sc / 2 : e.y - s.h * sc;
    if (flashPass) {
        if (e.flash > 0) gfx::draw(s, left, top, flip, sc, true, argb(0xffffff, 230));
        else if (e.burn > 0) gfx::draw(s, left, top, flip, sc, true, argb(0xff7020, uint32_t(70 + 40 * std::sin(time * 20))));
        // Glowing eyes and pustules baked into the art
        for (const GlowPx& g : s.glow) {
            int gx = flip ? s.w - 1 - g.x : g.x;
            float px = std::floor(left) + gx * sc, py = std::floor(top) + g.y * sc;
            float pulse = g.strong ? 1.f : 0.6f + 0.4f * std::sin(time * 4 + g.x + e.id);
            gfx::fillRect(px, py, sc, sc, argb(g.col, uint32_t(255 * pulse)));
            glow(px + sc / 2, py + sc / 2, (g.strong ? 4.f : 3.f) * sc, g.col, (g.strong ? 0.55f : 0.3f) * pulse);
        }
        return;
    }
    gfx::draw(s, left, top, flip, sc);
    if (e.t == E_RATKING) {
        float cs = 2;
        gfx::draw(A.crown, std::floor(left) + 10 * sc - A.crown.w * cs / 2, std::floor(top) + 2 * sc - A.crown.h * cs + 2, false, cs);
    }
    bool showBar = !isBoss(e.t) && (d.scale >= 2 || e.t == E_BRUTE || e.t == E_BLOATER || e.t == E_SPITTER) && e.hp < e.maxHp;
    if (showBar) {
        SDL_FRect b = enemyBox(e);
        bar(e.x - 8, b.y - 5, 16, 2, e.hp / e.maxHp, 0xd04040, 0x2a0a10);
    }
}

void Game::drawDoctor(float x, float y, float scale, int skin, float ang, bool left, bool locked) {
    int f = int(time * 1.7f) & 1;
    const Sprite& b = A.doctor[skin][f];
    uint32_t tint = locked ? 0xFF303038 : 0xFFFFFFFF;
    if (dying && mode != Mode::Title) {
        float rot = std::min(1.f, deathT * 2.5f) * PI / 2 * (left ? 1 : -1);
        gfx::drawRotated(b, x, y, b.w / 2.f, float(b.h), rot, false, scale, tint);
        return;
    }
    gfx::draw(b, x - b.w * scale / 2, y - b.h * scale, !left, scale, false, tint);
    float lx = left ? DOC_SHOULDER_X : b.w - DOC_SHOULDER_X;
    float rc = mode == Mode::Title ? 0 : recoil;
    float sx = x - b.w * scale / 2 + lx * scale - std::cos(ang) * rc * scale;
    float sy = y - b.h * scale + (DOC_SHOULDER_Y + f) * scale - std::sin(ang) * rc * scale;
    const ArmGfx& a = A.arm[skin][mode == Mode::Title ? 0 : cur];
    gfx::drawRotated(a.spr, sx, sy, a.px, a.py, ang, left, scale, tint);
}

void Game::drawBackground() {
    gfx::tex(A.sky, 0, 0, VW, VH);
    for (auto& s : A.stars) {
        float a = 0.5f + 0.5f * std::sin(time * 2.3f + s.phase * 3);
        gfx::fillRect(s.x, s.y, 1, 1, argb(s.col, uint32_t(a * 255)));
    }
    for (int i = 0; i < 3; i++) gfx::draw(A.cloud[i], cloudX[i], cloudY[i]);
    gfx::tex(A.town, 0, 0, VW, VH);
    gfx::tex(A.ground, 0, GROUND_Y, VW, VH - GROUND_Y);
    for (float lx : LAMP_X) gfx::drawFeet(A.lamp, lx, LAMP_FEET);

    gfx::tex(A.clinic, 334, 100, 50, 94);
    gfx::fillRect(318, 117, 16, 1, argb(0x1a1418));
    float sa = std::sin(time * 1.3f) * 0.10f + (raining ? std::sin(time * 3.1f) * 0.07f : 0);
    gfx::drawRotated(A.sign, 325, 118, A.sign.w / 2.f, 1, sa, false);
    gfx::fillRect(334, 144, 2, 1, argb(0x1a1418));
    gfx::drawFeet(A.lantern, 334, 152);

    gfx::tex(decalTex, 0, 0, VW, VH);
    fogLayer(146, fogX, 70);
}

void Game::drawEntities() {
    // Shadows
    auto shadow = [&](float x, float y, float w, uint32_t a) { gfx::tex(A.shadow, x - w / 2, y - 3, w, 6, argb(0xffffff, a)); };
    for (auto& e : enemies) {
        const EnemyDef& d = EDEF[e.t];
        if (d.fly) shadow(e.x, LANE_BOT - 4, 8, 60);
        else shadow(e.x, e.y, d.w * d.scale + 4, 200);
    }
    for (auto& c : critters) shadow(c.x, c.y, 14, 200);
    if (mode != Mode::Title) shadow(DOC_X, DOC_Y, 20, 220);
    for (auto& f : flaskList) shadow(lerp(f.x0, f.x1, f.t / f.dur), lerp(f.y0 + 8, f.y1, f.t / f.dur), 6, 120);

    // Depth-sorted drawables
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

    // World particles (lit)
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
        gfx::drawRotated(A.flask, x, y, A.flask.w / 2.f, A.flask.h / 2.f, f.t * 14, false);
    }

    for (auto& d : rain) gfx::line(d.x, d.y, d.x - 1, d.y - 4, argb(0x9ab0d8, 120));
    fogLayer(184, fogX * 1.7f + 40, 55);
}

// ============================================================ lighting

void Game::buildLightmap() {
    SDL_SetRenderTarget(R, lightTex);
    float fl = 0;
    if (flashT > 0) fl = (flashT > 0.28f || (flashT < 0.2f && flashT > 0.12f)) ? 1.f : 0.35f;
    uint32_t amb = mixRGB(0x6e6a98, 0xd8d8ff, fl * 0.85f);
    SDL_SetRenderDrawColor(R, (amb >> 16) & 255, (amb >> 8) & 255, amb & 255, 255);
    SDL_RenderClear(R);

    auto L = [&](float x, float y, float r, uint32_t col, float a) {
        gfx::tex(A.light, x - r, y - r, r * 2, r * 2, argb(col, uint32_t(clampf(a, 0, 1) * 255)), SDL_BLENDMODE_ADD);
    };
    L(62, 46, 170, 0x5a5a90, 0.7f);
    for (float lx : LAMP_X) {
        float f = 0.85f + 0.15f * std::sin(time * 13 + lx) * std::sin(time * 7.3f);
        L(lx, 128, 74, 0xffb060, 0.9f * f);
    }
    float lf = 0.85f + 0.15f * std::sin(time * 11) * std::sin(time * 5.1f);
    L(334, 150, 62, 0xffa040, 0.95f * lf);
    L(345, 162, 34, 0xffa050, 0.5f);
    L(362, 137, 30, 0xffa050, 0.4f);
    if (mode != Mode::Title) {
        float ex = DOC_X - 9 + (faceLeft ? DOC_EYE_X : 18 - DOC_EYE_X);
        L(ex, DOC_Y - 28 + DOC_EYE_Y, 12, SKINS[save.skin].eye, 0.6f);
        L(DOC_X - 6, DOC_Y - 14, 48, 0xffb070, 0.45f);
        if (muzzleT > 0) {
            float x, y;
            muzzle(x, y);
            L(x, y, WEAPONS[cur].flame ? 50.f : 95.f, 0xffc070, 1);
        }
    }
    for (auto& b : bullets) L(b.x, b.y, b.flame ? 22.f : 12.f, b.col, b.flame ? 0.22f : 0.45f);
    for (auto& f : fires) L(f.x, f.y - 6, 44, 0xff8030, 0.7f * clampf(f.life, 0, 1));
    for (auto& e : enemies) {
        SDL_FRect bx = enemyBox(e);
        if (e.burn > 0) L(bx.x + bx.w / 2, bx.y + bx.h / 2, 32, 0xff8030, 0.6f);
        if (e.t == E_BLOATER || e.t == E_ABOM) L(bx.x + bx.w / 2, bx.y + bx.h / 2, bx.w * 1.6f, 0x9aff40, 0.25f);
        if (e.t == E_PLAGUERAT) L(e.x, e.y - 4, 12, 0x9aff40, 0.25f);
    }
    for (auto& t : tlights) L(t.x, t.y, t.r * (0.6f + 0.4f * t.life / t.max), t.col, t.life / t.max);
    for (auto& g : globs) L(g.x, g.y, 16, 0x9aff40, 0.55f);
    for (auto& p : pickups) L(p.x, p.y, 8, 0xffd040, 0.35f);
    for (auto& f : flaskList) {
        float u = f.t / f.dur;
        L(lerp(f.x0, f.x1, u), lerp(f.y0, f.y1, u) - std::sin(PI * u) * 34, 18, 0xff8030, 0.5f);
    }
    int fireLights = 0;
    for (auto& p : parts)
        if (p.kind == 4 && (fireLights++ % 3) == 0) L(p.x, p.y, p.size * 2.5f, 0xff7020, 0.3f * p.life / p.max);

}

void Game::drawBackgroundEmissive() {
    // Lit windows
    for (auto& w : A.windows) {
        float f = 0.8f + 0.2f * std::sin(time * 1.7f + w.phase) * std::sin(time * 3.1f + w.phase * 2);
        uint32_t c = mixRGB(0x000000, w.col, f);
        gfx::fillRect(w.x, w.y, w.w, w.h, argb(c, 235));
        if (w.w >= 5 && w.h >= 5) {
            gfx::fillRect(w.x + std::floor(w.w / 2), w.y, 1, w.h, argb(0x2a1c14));
            gfx::fillRect(w.x, w.y + std::floor(w.h / 2), w.w, 1, argb(0x2a1c14));
        }
        glow(w.x + w.w / 2, w.y + w.h / 2, std::max(w.w, w.h) * 1.8f + 2, w.col, 0.25f * f);
    }
    // Lamp glass + lantern
    for (float lx : LAMP_X) {
        float f = 0.85f + 0.15f * std::sin(time * 13 + lx) * std::sin(time * 7.3f);
        float gx = lx - A.lamp.w / 2.f + 3, gy = LAMP_FEET - A.lamp.h + 3;
        gfx::fillRect(gx, gy, 1, 6, argb(0xffe0a0, uint32_t(255 * f)));
        gfx::fillRect(gx + 2, gy, 1, 6, argb(0xffe0a0, uint32_t(255 * f)));
        glow(gx + 1.5f, gy + 3, 16, 0xffb050, 0.55f * f);
    }
    glow(334, 149, 12, 0xffa040, 0.7f);
    glow(325, 124, 7, 0x6aff7a, 0.25f);

    // Lightning bolt
    if (flashT > 0.2f && bolt.size() > 1) {
        for (size_t i = 1; i < bolt.size(); i++) {
            gfx::line(bolt[i - 1].first, bolt[i - 1].second, bolt[i].first, bolt[i].second, argb(0xf0f0ff));
            gfx::line(bolt[i - 1].first + 1, bolt[i - 1].second, bolt[i].first + 1, bolt[i].second, argb(0x8a8aff, 120));
        }
    }
}

void Game::drawEmissive() {
    // The hero is drawn unlit so he always reads clearly against the night.
    if (mode != Mode::Title) drawDoctor(DOC_X, DOC_Y, 1, save.skin, aimAng, faceLeft, false);

    // Doctor lens
    if (mode != Mode::Title && !dying) {
        int f = int(time * 1.7f) & 1;
        float lx = faceLeft ? 7 : 18 - 9;
        gfx::fillRect(DOC_X - 9 + lx, DOC_Y - 28 + 8 + f, 2, 2, argb(SKINS[save.skin].eye));
        glow(DOC_X - 9 + lx + 1, DOC_Y - 28 + 9 + f, 6, SKINS[save.skin].eye, 0.8f);
    }

    for (auto& e : enemies) drawEnemy(e, true);

    // Projectiles
    for (auto& b : bullets) {
        if (b.flame) {
            float u = 1 - b.life / b.maxLife;
            float sz = 4 + u * 14;
            uint32_t c = u < 0.3f ? 0xfff0a0 : (u < 0.6f ? 0xffa030 : 0xc04010);
            gfx::tex(A.puff, b.x - sz / 2, b.y - sz / 2, sz, sz, argb(c, uint32_t((1 - u) * 200)), SDL_BLENDMODE_ADD);
        } else {
            float tl = cur == 2 ? 0.022f : 0.016f;
            gfx::line(b.x - b.vx * tl, b.y - b.vy * tl, b.x, b.y, argb(b.col, 200));
            gfx::fillRect(b.x, b.y, 1, 1, argb(0xffffff));
            glow(b.x, b.y, b.perfect ? 9.f : 6.f, b.col, b.perfect ? 0.8f : 0.5f);
        }
    }
    if (muzzleT > 0 && mode == Mode::Play && !dying) {
        float x, y;
        muzzle(x, y);
        gfx::drawRotated(A.muzzle, x, y, 0, 3.5f, aimAng, false, WEAPONS[cur].flame ? 0.7f : 1.f, 0xFFFFFFFF, SDL_BLENDMODE_ADD);
        glow(x, y, 18, 0xffc060, 0.8f);
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
        gfx::draw(A.glob, g.x - 3, g.y - 3);
        glow(g.x, g.y, 7, 0x9aff40, 0.6f);
    }
    for (auto& p : pickups) {
        gfx::draw(A.coin[(int(time * 8 + p.x) & 3) == 0 ? 1 : 0], p.x - 3, p.y - 3);
        if ((int(time * 10 + p.x) % 7) == 0) gfx::fillRect(p.x + 1, p.y - 2, 1, 1, argb(0xffffff));
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

void Game::panel(float x, float y, float w, float h) {
    gfx::fillRect(x, y, w, h, argb(0x140f1a, 240));
    gfx::rect(x, y, w, h, argb(0x8a6a3a));
    gfx::rect(x + 2, y + 2, w - 4, h - 4, argb(0x3a2a1e));
    for (auto [cx, cy] : {std::pair{x, y}, {x + w - 3, y}, {x, y + h - 3}, {x + w - 3, y + h - 3}})
        gfx::fillRect(cx, cy, 3, 3, argb(0xe0b040));
}

bool Game::button(float x, float y, float w, float h, const std::string& label, bool enabled, uint32_t col) {
    bool hov = enabled && inside(mx, my, x, y, w, h);
    gfx::fillRect(x, y, w, h, argb(hov ? 0x4a3628 : 0x221a20, 240));
    gfx::rect(x, y, w, h, argb(enabled ? (hov ? 0xffd860 : 0x8a6a3a) : 0x3a3036));
    font::draw(label, x + w / 2, y + std::floor((h - 5) / 2), argb(enabled ? (hov ? 0xffffff : col) : 0x5a5058), 1, 1);
    if (hov && clickPending) {
        clickPending = false;
        audio::play(SFX_CLICK, 0.5f);
        return true;
    }
    return false;
}

void Game::drawCursor() {
    if (mode == Mode::Play && !dying) {
        float g = 2 + std::round(crossSpread);
        uint32_t c = reloading ? argb(0xa0a0a0) : argb(0xffffff);
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
        if (reloading) {
            float p = reloadT / reloadDur;
            gfx::fillRect(x - 6, y + g + 6, 12, 2, d);
            gfx::fillRect(x - 6, y + g + 6, std::floor(12 * clampf(p, 0, 1)), 1, argb(0xffd860));
        }
    } else {
        gfx::draw(A.cursor, std::floor(mx) - 1, std::floor(my) - 1);
    }
}

// ============================================================ HUD

void Game::drawHud() {
    // Health + barricade
    gfx::draw(A.heart, 4, 3);
    bar(15, 5, 64, 5, hp / maxHp, 0xd02a3a, 0x3a1018);
    font::draw(num(int(std::ceil(hp))) + "/" + num(int(maxHp)), 84, 5, argb(0xffc0c0));
    gfx::draw(A.plank, 4, 13);
    bar(15, 15, 64, 5, barrHp / barrMax, barrTier == 2 ? 0x9aa0aa : 0xc88a3a, 0x2a1a10);
    if (barrHp <= 0) {
        if (int(time * 4) & 1) font::draw("BROKEN!", 84, 15, argb(0xff4040));
    } else {
        font::draw(num(int(std::ceil(barrHp))), 84, 15, argb(0xe8c890));
    }

    // Wave + progress
    font::draw("WAVE " + num(wave), VW / 2, 4, argb(0xffd860), 1, 1);
    bar(VW / 2 - 40, 12, 80, 2, waveTotal > 0 ? float(waveKilled) / waveTotal : 0, 0x9a7ad8, 0x1a1428);

    float comboY = 20;
    for (auto& e : enemies) {
        if (!isBoss(e.t)) continue;
        font::draw(EDEF[e.t].name, VW / 2, 19, argb(0xff8080), 1, 1, true);
        bar(VW / 2 - 70, 27, 140, 4, e.hp / e.maxHp, 0xc02040, 0x2a0a10);
        comboY = 36;
        break;
    }

    // Coins + kills
    float cp = coinPulse > 0 ? 1 : 0;
    gfx::draw(A.coin[0], HUD_COIN_X - 3, HUD_COIN_Y - 3 - cp);
    font::draw(num(coins), VW - 6, 6, argb(coinPulse > 0 ? 0xffffff : 0xffd860), 1, 2);
    gfx::draw(A.skull, HUD_COIN_X - 3, 15);
    font::draw(num(runKills), VW - 6, 16, argb(0xe8e0cc), 1, 2);

    // Combo
    if (combo >= 2) {
        int sc = combo >= 10 ? 2 : 1;
        uint32_t c = combo >= 20 ? 0xff60ff : (combo >= 10 ? 0xff9a30 : 0xffe060);
        float wob = std::sin(time * 18) * (combo >= 10 ? 1.f : 0.f);
        font::draw("X" + num(combo) + " COMBO", VW / 2, comboY + wob, argb(c), sc, 1, true);
        bar(VW / 2 - 20, comboY + 6 * sc + 2, 40, 1, comboT / 2.2f, c, 0x1a1428);
    }

    // Weapon slots
    for (int i = 0; i < WEAPON_COUNT; i++) {
        SDL_FRect r = slotRect(i);
        bool sel = i == cur;
        gfx::fillRect(r.x, r.y, r.w, r.h, argb(sel ? 0x3a2a1a : 0x140f1a, 220));
        gfx::rect(r.x, r.y, r.w, r.h, argb(sel ? 0xffd860 : (owned[i] ? 0x6a5a4a : 0x2e2830)));
        const Sprite& ic = A.weaponIcon[i];
        gfx::draw(ic, r.x + std::floor((r.w - ic.w) / 2), r.y + std::floor((r.h - ic.h) / 2) + 1, false, 1, false,
                  owned[i] ? 0xFFFFFFFF : argb(0x404048, 200));
        font::draw(num(i + 1), r.x + 2, r.y + 2, argb(sel ? 0xffd860 : 0x8a7a6a), 1, 0, false);
        if (!owned[i]) gfx::draw(A.lock, r.x + r.w - 8, r.y + r.h - 8);
    }
    // Ammo
    {
        const WeaponDef& d = WEAPONS[cur];
        float ay = VH - 30;
        int clip = clipSize(cur);
        if (d.flame) {
            bar(6, ay + 1, 60, 3, float(ammo[cur]) / clip, 0xff8a30, 0x2a1408);
            font::draw("FUEL", 70, ay, argb(0xffb070));
        } else {
            for (int k = 0; k < clip; k++) {
                bool full = k < ammo[cur];
                uint32_t c = full ? (blessedShots > 0 ? 0xfff080 : 0xd8b048) : 0x3a2e28;
                gfx::fillRect(6 + k * 4, ay, 2, 5, argb(0x0d0a12));
                gfx::fillRect(6 + k * 4, ay, 2, 4, argb(c));
                if (full) gfx::fillRect(6 + k * 4, ay, 2, 1, argb(0xfff4c0));
            }
        }
        if (reloading)
            font::draw(jammed ? "JAMMED..." : "RELOADING - PRESS R!", 6 + std::min(clip, 16) * 4 + 4, ay, argb(jammed ? 0xff6050 : 0xc8c0b0));
        if (blessedShots > 0) font::draw("BLESSED X" + num(blessedShots), 6, ay - 8, argb(0xfff080));
        font::draw(d.name, 6 + 5 * 25 + 2, VH - 12, argb(0xa89880));
    }

    // Consumables
    struct Item { const Sprite* s; int n; const char* key; };
    const Item items[3] = {{&A.flask, flasks, "RMB"}, {&A.trapOpen, traps, "E"}, {&A.tonic, tonics, "Q"}};
    for (int k = 0; k < 3; k++) {
        float x = VW - 96 + k * 31, y = VH - 21;
        gfx::fillRect(x, y, 29, 16, argb(0x140f1a, 220));
        gfx::rect(x, y, 29, 16, argb(items[k].n > 0 ? 0x6a5a4a : 0x2e2830));
        const Sprite& s = *items[k].s;
        gfx::draw(s, x + 2, y + std::floor((16 - s.h) / 2), false, 1, false, items[k].n > 0 ? 0xFFFFFFFF : argb(0x505058));
        font::draw(num(items[k].n), x + 27, y + 9, argb(0xffffff), 1, 2);
        font::draw(items[k].key, x + 27, y + 2, argb(0x8a7a6a), 1, 2, false);
    }

    // Active-reload gauge over the doctor
    if (reloading && !dying) {
        float bx = DOC_X - 15, by = DOC_Y - 40, bw = 30;
        float lo = save.skin == 5 ? 0.36f : 0.42f, hi = save.skin == 5 ? 0.68f : 0.60f;
        gfx::fillRect(bx - 1, by - 1, bw + 2, 6, argb(0x0d0a12));
        gfx::fillRect(bx, by, bw, 4, argb(0x2a2430));
        gfx::fillRect(bx + std::floor(bw * lo), by, std::ceil(bw * (hi - lo)), 4, argb(jammed ? 0x6a2020 : 0xc8a030));
        float p = clampf(reloadT / reloadDur, 0, 1);
        gfx::fillRect(bx + std::floor(bw * p), by - 1, 1, 6, argb(0xffffff));
    }

    // Popups
    for (auto& p : popups) {
        float a = clampf(p.life / p.max * 2.5f, 0, 1);
        font::draw(p.s, p.x, p.y, argb(p.col, uint32_t(a * 255)), p.scale, 1);
    }

    // Banner
    if (bannerT > 0) {
        float u = 1 - bannerT / bannerMax;
        float a = std::min({1.f, u * 6, bannerT * 2.5f});
        float slide = (1 - std::min(1.f, u * 5)) * 30;
        float y = 62;
        gfx::fillRect(0, y - 8, VW, 34, argb(0x000000, uint32_t(110 * a)));
        font::draw(bannerText, VW / 2 - slide, y, argb(bannerCol, uint32_t(255 * a)), 3, 1, true);
        if (!bannerSub.empty()) font::draw(bannerSub, VW / 2 + slide, y + 19, argb(0xd8d0c0, uint32_t(255 * a)), 1, 1);
    }
    // Toasts
    float ty = 100;
    for (auto& t : toasts) {
        float a = clampf(t.life, 0, 1);
        font::draw(t.s, VW / 2, ty, argb(t.col, uint32_t(255 * a)), 1, 1, true);
        ty += 9;
    }
    // First-wave tutorial hints
    if (wave == 1 && waveT < 12 && mode == Mode::Play) {
        float a = clampf(12 - waveT, 0, 1);
        uint32_t c = argb(0xc8c0b0, uint32_t(200 * a));
        font::draw("CLICK TO SHOOT - AIM AT HEADS FOR CRITS", VW / 2, 132, c, 1, 1);
        font::draw("PRESS R IN THE GOLD ZONE FOR A PERFECT RELOAD", VW / 2, 140, c, 1, 1);
    }
}

// ============================================================ shop

void Game::drawShop() {
    gfx::fillRect(0, 0, VW, VH, argb(0x07050c, 150));
    panel(8, 6, VW - 16, VH - 12);
    font::draw("THE APOTHECARY", VW / 2, 13, argb(0xffd860), 2, 1, true);
    gfx::draw(A.coin[0], VW - 64, 13);
    font::draw(num(coins), VW - 18, 14, argb(0xffd860), 1, 2);
    font::draw("WAVE " + num(wave) + " SURVIVED", 18, 14, argb(0x8aff8a));

    std::string desc;
    const float colX[3] = {16, 136, 256}, rowY0 = 42, rowH = 23, colW = 112;
    const char* heads[3] = {"ARSENAL", "UPGRADES", "SUPPLIES"};
    for (int c = 0; c < 3; c++) {
        font::draw(heads[c], colX[c] + 2, 32, argb(0xc8a878), 1, 0, false);
        gfx::fillRect(colX[c], 38, colW, 1, argb(0x3a2a1e));
    }

    auto row = [&](int c, int i, bool active, bool selected) {
        float x = colX[c], y = rowY0 + i * rowH;
        bool hov = inside(mx, my, x, y, colW, rowH - 2);
        gfx::fillRect(x, y, colW, rowH - 2, argb(selected ? 0x23331f : (hov ? 0x33263a : 0x1c1624), 235));
        gfx::rect(x, y, colW, rowH - 2, argb(hov ? 0xffd860 : (selected ? 0x5a8a4a : 0x3a2e36)));
        (void)active;
        bool clicked = hov && clickPending;
        if (clicked) clickPending = false;
        return std::pair<bool, bool>{hov, clicked};
    };
    auto price = [&](float x, float y, int cost) {
        gfx::draw(A.coin[0], x, y - 1);
        font::draw(num(cost), x + 9, y, argb(coins >= cost ? 0xffd860 : 0xc05050), 1, 0, false);
    };

    // Arsenal
    for (int i = 0; i < WEAPON_COUNT; i++) {
        const WeaponDef& d = WEAPONS[i];
        auto [hov, clicked] = row(0, i, true, owned[i] && cur == i);
        float x = colX[0], y = rowY0 + i * rowH;
        const Sprite& ic = A.weaponIcon[i];
        gfx::draw(ic, x + 3, y + std::floor((rowH - 2 - ic.h) / 2));
        font::draw(d.name, x + 25, y + 4, argb(0xf0e8d8), 1, 0, false);
        if (owned[i]) font::draw(cur == i ? "EQUIPPED" : "OWNED - EQUIP", x + 25, y + 12, argb(cur == i ? 0x8aff8a : 0x9a9080), 1, 0, false);
        else price(x + 25, y + 12, d.price);
        if (hov) {
            char buf[96];
            if (d.flame) std::snprintf(buf, sizeof buf, "  DMG %.1f/TICK  FUEL %d  BURNS", d.dmg * dmgMul(), clipSize(i));
            else std::snprintf(buf, sizeof buf, "  DMG %dX%d  CLIP %d  RELOAD %.1fS", int(d.dmg * dmgMul()), d.pellets, clipSize(i), d.reload / rateMul());
            desc = std::string(d.desc) + buf;
        }
        if (clicked) {
            if (owned[i]) {
                cur = i;
                audio::play(SFX_CLICK, 0.5f);
            } else if (coins >= d.price) {
                coins -= d.price;
                owned[i] = true;
                ammo[i] = clipSize(i);
                cur = i;
                audio::play(SFX_BUY, 0.7f);
                toast("ACQUIRED " + std::string(d.name), 0xffd860);
            } else {
                audio::play(SFX_EMPTY, 0.5f);
            }
        }
    }

    // Upgrades
    for (int i = 0; i < 5; i++) {
        const UpgradeDef& u = UPGRADES[i];
        auto [hov, clicked] = row(1, i, true, false);
        float x = colX[1], y = rowY0 + i * rowH;
        font::draw(u.name, x + 4, y + 4, argb(0xf0e8d8), 1, 0, false);
        for (int k = 0; k < u.max; k++) {
            gfx::fillRect(x + 60 + k * 6, y + 4, 4, 5, argb(0x0d0a12));
            gfx::fillRect(x + 60 + k * 6, y + 4, 4, 4, argb(k < up[i] ? 0x8aff8a : 0x3a3440));
        }
        bool maxed = up[i] >= u.max;
        int cost = upgradeCost(i);
        if (maxed) font::draw("MASTERED", x + 4, y + 12, argb(0x8aff8a), 1, 0, false);
        else price(x + 4, y + 12, cost);
        if (hov) desc = u.desc;
        if (clicked) {
            if (!maxed && coins >= cost) {
                coins -= cost;
                up[i]++;
                if (i == 2)
                    for (int w = 0; w < WEAPON_COUNT; w++) ammo[w] = clipSize(w);
                if (i == 3) {
                    maxHp += 25;
                    hp = std::min(maxHp, hp + 25);
                }
                if (i == 4) {
                    barrTier = up[4];
                    barrMax = barrMaxFor(barrTier);
                    barrHp = barrMax;
                }
                audio::play(SFX_BUY, 0.7f);
            } else {
                audio::play(SFX_EMPTY, 0.5f);
            }
        }
    }

    // Supplies
    for (int i = 0; i < 5; i++) {
        const SupplyDef& s = SUPPLIES[i];
        auto [hov, clicked] = row(2, i, true, false);
        float x = colX[2], y = rowY0 + i * rowH;
        font::draw(s.name, x + 4, y + 4, argb(0xf0e8d8), 1, 0, false);
        int cost = s.price;
        bool can = true;
        std::string status;
        if (i == 0) {
            cost = std::max(10, int((barrMax - barrHp) * 0.6f));
            can = barrHp < barrMax;
            if (!can) status = "INTACT";
            bar(x + 70, y + 13, 36, 3, barrHp / barrMax, 0xc88a3a, 0x2a1a10);
        } else if (i == 4) {
            cost = std::max(5, int((maxHp - hp) * 0.8f));
            can = hp < maxHp;
            if (!can) status = "HEALTHY";
            bar(x + 70, y + 13, 36, 3, hp / maxHp, 0xd02a3a, 0x3a1018);
        } else {
            int have = i == 1 ? flasks : (i == 2 ? traps : tonics);
            can = have < s.max;
            font::draw(num(have) + "/" + num(s.max), x + colW - 4, y + 4, argb(0xa89880), 1, 2, false);
            if (!can) status = "FULL";
        }
        if (!status.empty()) font::draw(status, x + 4, y + 12, argb(0x8aff8a), 1, 0, false);
        else price(x + 4, y + 12, cost);
        if (hov) desc = s.desc;
        if (clicked) {
            if (can && coins >= cost) {
                coins -= cost;
                if (i == 0) barrHp = barrMax;
                else if (i == 1) flasks++;
                else if (i == 2) traps++;
                else if (i == 3) tonics++;
                else hp = maxHp;
                audio::play(i == 4 ? SFX_DRINK : SFX_BUY, 0.7f);
            } else {
                audio::play(SFX_EMPTY, 0.5f);
            }
        }
    }

    gfx::fillRect(16, 160, VW - 32, 1, argb(0x3a2a1e));
    if (desc.empty()) desc = "HOVER AN ITEM FOR DETAILS. COINS ARE KEPT BETWEEN WAVES.";
    font::draw(desc, 18, 165, argb(0xd8d0c0), 1, 0, false);
    int pct = int(clampf(hp / maxHp, 0, 1) * 100);
    font::draw("HEALTH " + num(pct) + "%   BEST WAVE " + num(std::max(save.bestWave, wave)), 18, 185, argb(0x9a9080), 1, 0, false);
    if (button(VW - 150, 180, 134, 16, "DEFEND THE CLINIC >", true, 0xffd860)) startWave();

    for (auto& t : toasts) {
        font::draw(t.s, VW / 2, 174, argb(t.col, uint32_t(255 * clampf(t.life, 0, 1))), 1, 1, true);
        break;
    }
}

// ============================================================ title / pause / game over

void Game::drawTitle() {
    gfx::fillRect(0, 0, VW, VH, argb(0x05030a, 60));
    // Wavy two-line logo: the full name is too wide for one line at scale 4.
    auto wavy = [&](const std::string& s, float y, int scale, float phase) {
        float x0 = VW / 2 - font::width(s, scale) / 2.f;
        for (size_t i = 0; i < s.size(); i++) {
            float yo = std::sin(time * 2.5f + i * 0.45f + phase) * (scale >= 4 ? 2.f : 1.f);
            uint32_t c = mixRGB(0xffd860, 0xd84a2a, 0.5f + 0.5f * std::sin(time * 1.5f + i * 0.3f + phase));
            font::draw(std::string(1, s[i]), x0 + i * 4 * scale, y + yo, argb(c), scale, 0, true);
        }
    };
    wavy("ZE CLINIC", 8, 4, 0);
    wavy("IF HE BORNED IN A MEDIAVEL ERA", 32, 2, 1.7f);
    font::draw("A PLAGUE DOCTOR'S LAST STAND", VW / 2, 46, argb(0xa8c890), 1, 1, true);

    // Wardrobe
    float cx = 104, fy = 150;
    bool locked = !skinUnlocked(previewSkin);
    gfx::tex(A.shadow, cx - 30, fy - 5, 60, 10, argb(0xffffff, 200));
    float sxs = cx - 27 + DOC_SHOULDER_X * 3, sys = fy - 28 * 3 + DOC_SHOULDER_Y * 3;
    float ang = std::atan2(my - sys, mx - sxs);
    bool left = std::cos(ang) < 0;
    drawDoctor(cx, fy, 3, previewSkin, ang, left, locked);
    if (!locked) {
        int f = int(time * 1.7f) & 1;
        float lx = left ? 7 : 18 - 9;
        float ex = cx - 27 + lx * 3, ey = fy - 84 + (8 + f) * 3;
        gfx::fillRect(ex, ey, 6, 6, argb(SKINS[previewSkin].eye));
        glow(ex + 3, ey + 3, 10, SKINS[previewSkin].eye, 0.5f);
    }
    const Skin& sk = SKINS[previewSkin];
    font::draw(sk.name, cx, fy + 6, argb(locked ? 0x8a8080 : 0xffd860), 1, 1, true);
    font::draw(sk.perk, cx, fy + 15, argb(locked ? 0x6a6060 : 0xa8e0a0), 1, 1);
    if (locked) {
        gfx::draw(A.lock, cx - 3, fy - 50);
        font::draw("LOCKED: " + sk.unlock, cx, fy + 24, argb(0xff7060), 1, 1);
    } else if (previewSkin == save.skin) {
        font::draw("EQUIPPED", cx, fy + 24, argb(0x8aff8a), 1, 1);
    }
    font::draw(num(previewSkin + 1) + "/" + num(SKIN_COUNT), cx, 56, argb(0x9a9080), 1, 1);
    if (button(cx - 62, 92, 14, 16, "<")) previewSkin = (previewSkin + SKIN_COUNT - 1) % SKIN_COUNT;
    if (button(cx + 48, 92, 14, 16, ">")) previewSkin = (previewSkin + 1) % SKIN_COUNT;
    if (skinUnlocked(previewSkin) && previewSkin != save.skin) {
        save.skin = previewSkin;
        persist();
    }

    // Menu
    float bx = 236, bw = 128;
    if (button(bx, 70, bw, 18, "BEGIN THE PURGE", true, 0xffd860)) startRun();
    if (button(bx, 94, bw, 15, audio::muted() ? "SOUND: OFF  (M)" : "SOUND: ON  (M)")) {
        save.muted = !save.muted;
        audio::setMuted(save.muted);
        persist();
    }
    if (button(bx, 113, bw, 15, "FULLSCREEN  (F11)")) {
        bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
        SDL_SetWindowFullscreen(win, !fs);
    }
    if (button(bx, 132, bw, 15, "QUIT")) quit = true;
    font::draw("BEST WAVE  " + num(save.bestWave), bx + 4, 154, argb(0xc8b8a0), 1, 0, false);
    font::draw("VERMIN SLAIN  " + num(save.totalKills), bx + 4, 162, argb(0xc8b8a0), 1, 0, false);
    font::draw("BOSSES SLAIN  " + num(save.bossKills), bx + 4, 170, argb(0xc8b8a0), 1, 0, false);

    gfx::fillRect(0, VH - 14, VW, 14, argb(0x000000, 150));
    font::draw("LMB SHOOT  RMB FLASK  E TRAP  Q TONIC  R RELOAD  1-5 ARMS  ESC PAUSE", VW / 2, VH - 10, argb(0x9a9080), 1, 1, false);
    for (auto& t : toasts) {
        font::draw(t.s, VW / 2, 59, argb(t.col, uint32_t(255 * clampf(t.life, 0, 1))), 1, 1, true);
        break;
    }
}

void Game::drawPaused() {
    gfx::fillRect(0, 0, VW, VH, argb(0x05030a, 170));
    font::draw("PAUSED", VW / 2, 50, argb(0xffd860), 3, 1, true);
    float bx = VW / 2 - 60;
    if (button(bx, 86, 120, 16, "RESUME")) mode = Mode::Play;
    if (button(bx, 106, 120, 16, audio::muted() ? "SOUND: OFF" : "SOUND: ON")) {
        save.muted = !save.muted;
        audio::setMuted(save.muted);
        persist();
    }
    if (button(bx, 126, 120, 16, "ABANDON RUN")) {
        persist();
        mode = Mode::Title;
        enemies.clear();
        bullets.clear();
        globs.clear();
        flaskList.clear();
        trapList.clear();
        fires.clear();
        pickups.clear();
        decalClear = true;
    }
}

void Game::drawGameOver() {
    gfx::fillRect(0, 0, VW, VH, argb(0x1a0408, 170));
    font::draw("THE PLAGUE PREVAILS", VW / 2, 36, argb(0xff4a4a), 3, 1, true);
    font::draw("THE CLINIC HAS FALLEN. THE RATS FEAST TONIGHT.", VW / 2, 60, argb(0xc8a8a0), 1, 1);
    float y = 80;
    auto line = [&](const std::string& k, const std::string& v) {
        font::draw(k, VW / 2 - 4, y, argb(0xa89890), 1, 2, false);
        font::draw(v, VW / 2 + 4, y, argb(0xffffff), 1, 0, false);
        y += 10;
    };
    line("REACHED WAVE", num(wave));
    line("VERMIN SLAIN", num(runKills));
    line("BEST COMBO", "X" + num(bestCombo));
    line("BOSSES SLAIN", num(bossesThisRun));
    line("BEST WAVE EVER", num(save.bestWave));
    if (newBest && int(time * 3) % 2 == 0) font::draw("NEW RECORD!", VW / 2, y + 2, argb(0x8aff8a), 1, 1, true);
    if (button(VW / 2 - 126, 150, 120, 18, "TRY AGAIN", true, 0xffd860)) startRun();
    if (button(VW / 2 + 6, 150, 120, 18, "RETURN TO TITLE")) mode = Mode::Title;
    for (auto& t : toasts) {
        font::draw(t.s, VW / 2, 176, argb(t.col, uint32_t(255 * clampf(t.life, 0, 1))), 1, 1, true);
        break;
    }
}

// ============================================================ dev autotest

void Game::autotest(int frame, const std::string& dir) {
    if (dir == "play") {
        // Honest bot: no cheats, simple shop policy. Logs one line per wave.
        static int lastWave = 0;
        static float skill = 0.6f; // chance of nailing the active reload
        if (mode == Mode::Title && frame > 2) startRun();
        if (mode == Mode::Play && !dying) {
            const Enemy* best = nullptr;
            float bestScore = -1e9f;
            int nearBarr = 0;
            for (auto& e : enemies) {
                if (e.dead || e.x < -4) continue;
                float score = e.x + (e.t == E_CROW && e.state == 2 ? 400 : 0) + (e.t == E_BLOATER ? 60 : 0);
                if (score > bestScore) { bestScore = score; best = &e; }
                if (e.x > BARR_X - 60) nearBarr++;
            }
            if (best) {
                SDL_FRect b = enemyBox(*best);
                mx = b.x + b.w / 2 + rnd(-3, 3);
                my = b.y + b.h * rnd(0.1f, 0.6f);
                lmb = true;
                fireQueue = 0.2f;
                if (nearBarr >= 4 && frame % 90 == 0) throwFlask();
            } else {
                lmb = false;
            }
            if (reloading && !jammed && frame % 7 == 0) {
                float p = reloadT / reloadDur;
                if (p > 0.45f && p < 0.55f) {
                    if (chance(skill)) tryActiveReload();
                    else jammed = true;
                }
            }
            if (hp < maxHp * 0.4f && tonics > 0) drinkTonic();
        }
        if (mode == Mode::Shop) {
            SDL_Log("wave %2d cleared  t=%5.1fs  coins=%5d  hp=%3.0f/%3.0f  barr=%3.0f/%3.0f  kills=%d", wave, waveT, coins, hp, maxHp,
                    barrHp, barrMax, runKills);
            lastWave = wave;
            auto buy = [&](int cost) { if (coins >= cost) { coins -= cost; return true; } return false; };
            if (barrHp < barrMax * 0.7f) { int c = std::max(10, int((barrMax - barrHp) * 0.6f)); if (buy(c)) barrHp = barrMax; }
            if (hp < maxHp * 0.6f) { int c = std::max(5, int((maxHp - hp) * 0.8f)); if (buy(c)) hp = maxHp; }
            for (int w = 1; w < WEAPON_COUNT; w++)
                if (!owned[w] && buy(WEAPONS[w].price)) { owned[w] = true; ammo[w] = clipSize(w); cur = w; }
            for (int i : {0, 1, 4, 2, 3})
                if (up[i] < UPGRADES[i].max && buy(upgradeCost(i))) {
                    up[i]++;
                    if (i == 3) { maxHp += 25; hp += 25; }
                    if (i == 4) { barrTier = up[4]; barrMax = barrMaxFor(barrTier); barrHp = barrMax; }
                }
            if (flasks < 3) if (buy(45)) flasks++;
            startWave();
        }
        if (mode == Mode::GameOver) {
            SDL_Log("GAME OVER at wave %d (last cleared %d), kills=%d, bestCombo=%d, sim time %.0fs", wave, lastWave, runKills, bestCombo, time);
            quit = true;
        }
        if (time > 1800) { SDL_Log("time cap at wave %d", wave); quit = true; }
        return;
    }
    auto shot = [&](const char* name) {
        SDL_SetRenderTarget(R, sceneTex);
        if (SDL_Surface* s = SDL_RenderReadPixels(R, nullptr)) {
            SDL_SaveBMP(s, (dir + "/" + name + ".bmp").c_str());
            SDL_DestroySurface(s);
        }
        SDL_SetRenderTarget(R, nullptr);
    };
    // Bot: aim at the closest enemy's head and keep shooting.
    if (mode == Mode::Play && !dying) {
        const Enemy* best = nullptr;
        for (auto& e : enemies)
            if (!e.dead && (!best || e.x > best->x)) best = &e;
        if (best) {
            SDL_FRect b = enemyBox(*best);
            mx = b.x + b.w / 2;
            my = b.y + 2;
            lmb = true;
            fireQueue = 0.2f;
        }
        if (frame % 240 == 120 && best) throwFlask();
        if (reloading && reloadT / reloadDur > 0.5f && !jammed) tryActiveReload();
    }
    switch (frame) {
    case 40: shot("01_title"); break;
    case 41: startRun(); break;
    case 420: shot("02_wave1"); break;
    case 700:
        for (auto& e : enemies) e.dead = true;
        spawnIdx = queue.size();
        break;
    case 900: shot("03_shop"); break;
    case 901:
        coins = 9000;
        for (bool& o : owned) o = true;
        save.skin = 6;
        wave = 9;
        startWave();
        spawnEnemy(E_ABOM, 70, 196, false);
        spawnEnemy(E_RATKING, 170, 186, false);
        spawnEnemy(E_BRUTE, 230, 178, false);
        spawnEnemy(E_SPITTER, 120, 174, false);
        spawnEnemy(E_BLOATER, 200, 204, false);
        spawnEnemy(E_CROW, 60, 60, false);
        break;
    case 1200: shot("04_boss_wave"); break;
    case 1201: cur = 3; break;
    case 1500: shot("05_censer"); break;
    case 1501: cur = 4; break;
    case 1650: shot("06_cannon"); break;
    case 1651: barrHp = 1; hitBarricade(5); break;
    case 1700: shot("07_broken"); break;
    case 1701: hurtDoctor(9999); break;
    case 1900: shot("08_gameover"); break;
    case 1910: quit = true; break;
    }
}
