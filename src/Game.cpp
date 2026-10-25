// Gameplay logic: run flow, waves, combat, enemies, effects.
#include "Defs.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>

using namespace util;

namespace {
const char* WAVE_FLAVOR[] = {"THE VERMIN STIR",    "THE MIASMA THICKENS", "HOLD THE LINE, DOCTOR",
                             "THEY SMELL MEDICINE", "PRAY FOR DAWN",       "THE BELLS TOLL FOR THEE"};

uint32_t darker(uint32_t c) { return mixRGB(c, 0x000000, 0.35f); }
} // namespace

// ============================================================ init / persistence

bool Game::init(SDL_Window* w, SDL_Renderer* r) {
    win = w;
    R = r;
    gfx::R = r;
    font::init();
    buildArt();
    auto mkTarget = [&](SDL_BlendMode m) {
        SDL_Texture* t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, VW, VH);
        if (t) {
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(t, m);
        }
        return t;
    };
    sceneTex = mkTarget(SDL_BLENDMODE_NONE);
    lightTex = mkTarget(SDL_BLENDMODE_MOD);
    decalTex = mkTarget(SDL_BLENDMODE_BLEND);
    entityTex = mkTarget(SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    if (!sceneTex || !lightTex || !decalTex || !entityTex) {
        SDL_Log("Render targets unavailable: %s", SDL_GetError());
        return false;
    }
    // Autotest runs must never touch the player's real progress.
    if (SDL_getenv("ZECLINIC_AUTOTEST")) {
    } else if (char* pref = SDL_GetPrefPath("PlagueWorks", "ZeClinic")) {
        savePath = std::string(pref) + "save.txt";
        SDL_free(pref);
    }
    load();
    for (int i = 0; i < SKIN_COUNT; i++) unlockedCache[i] = skinUnlocked(i);
    if (!skinUnlocked(save.skin)) save.skin = 0;
    previewSkin = save.skin;
    audio::setMuted(save.muted);
    audio::setMusic(true);
    raining = chance(0.5f);
    audio::setRain(raining);
    SDL_HideCursor();
    return true;
}

void Game::shutdown() {
    persist();
    for (SDL_Texture* t : {sceneTex, lightTex, decalTex, entityTex})
        if (t) SDL_DestroyTexture(t);
}

void Game::load() {
    if (savePath.empty()) return;
    std::ifstream f(savePath);
    std::string key;
    int v;
    while (f >> key >> v) {
        if (key == "best") save.bestWave = v;
        else if (key == "kills") save.totalKills = v;
        else if (key == "bosses") save.bossKills = v;
        else if (key == "skin") save.skin = std::clamp(v, 0, SKIN_COUNT - 1);
        else if (key == "muted") save.muted = v != 0;
    }
}

void Game::persist() {
    if (savePath.empty()) return;
    std::ofstream f(savePath);
    f << "best " << save.bestWave << "\nkills " << save.totalKills << "\nbosses " << save.bossKills << "\nskin " << save.skin
      << "\nmuted " << (save.muted ? 1 : 0) << "\n";
}

bool Game::skinUnlocked(int i) const {
    switch (i) {
    case 0: return true;
    case 1: return save.bestWave >= 3;
    case 2: return save.totalKills >= 150;
    case 3: return save.bestWave >= 8;
    case 4: return save.totalKills >= 500;
    case 5: return save.bestWave >= 12;
    case 6: return save.bossKills >= 3;
    }
    return false;
}

void Game::checkUnlocks(bool announce) {
    for (int i = 0; i < SKIN_COUNT; i++) {
        if (unlockedCache[i] || !skinUnlocked(i)) continue;
        unlockedCache[i] = true;
        if (announce) {
            toast("NEW SKIN UNLOCKED: " + SKINS[i].name, 0xffd860);
            audio::play(SFX_PERFECT, 0.6f, 0.8f);
        }
    }
}

// ============================================================ stats

int Game::clipSize(int w) const { return std::max(1, int(std::round(WEAPONS[w].clip * (1 + 0.34f * up[2])))); }
float Game::dmgMul() const { return (1 + 0.15f * up[0]) * (save.skin == 6 ? 1.1f : 1.f); }
float Game::coinMul() const { return save.skin == 4 ? 1.15f : (save.skin == 6 ? 1.1f : 1.f); }
float Game::critMul() const { return save.skin == 1 ? 2.5f : 2.f; }
float Game::rateMul() const { return 1 + 0.12f * up[1]; }
float Game::barrMaxFor(int tier) const {
    const float base[3] = {150, 260, 400};
    return base[std::clamp(tier, 0, 2)] * (save.skin == 3 ? 1.3f : 1.f);
}
int Game::upgradeCost(int i) const { return UPGRADES[i].base * (up[i] + 1); }

// ============================================================ run flow

void Game::startRun() {
    wave = 0;
    coins = 0;
    maxHp = 100;
    hp = maxHp;
    for (int& u : up) u = 0;
    barrTier = 0;
    barrMax = barrMaxFor(0);
    barrHp = barrMax;
    for (int w = 0; w < WEAPON_COUNT; w++) {
        owned[w] = w == 0;
        ammo[w] = clipSize(w);
    }
    cur = 0;
    reloading = jammed = false;
    blessedShots = 0;
    fireCD = 0;
    flasks = 2 + (save.skin == 2 ? 2 : 0);
    traps = 1;
    tonics = 1;
    runKills = combo = bestCombo = bossesThisRun = 0;
    comboT = 0;
    dying = false;
    deathT = 0;
    newBest = false;
    timeScale = 1;
    enemies.clear();
    pending.clear();
    bullets.clear();
    parts.clear();
    popups.clear();
    pickups.clear();
    flaskList.clear();
    trapList.clear();
    fires.clear();
    globs.clear();
    tlights.clear();
    critters.clear();
    toasts.clear();
    startWave();
}

void Game::buildWave() {
    queue.clear();
    bool boss = wave % 5 == 0;
    float budget = 6 + wave * 5.f + wave * wave * 0.35f;
    float t = 1.5f;
    if (boss) {
        budget *= 0.6f;
        queue.push_back({3.f, (wave / 5) % 2 == 1 ? E_RATKING : E_ABOM, (LANE_TOP + LANE_BOT) / 2});
    }
    struct Opt { EType t; float cost; int minW; float weight; };
    const Opt opts[] = {{E_RAT, 1, 1, 10},       {E_CROW, 2, 2, 3},       {E_PLAGUERAT, 2, 3, 4}, {E_BLOATER, 4, 4, 2.5f},
                        {E_GIANTRAT, 4, 5, 2.5f}, {E_SPITTER, 4, 6, 2.2f}, {E_BRUTE, 8, 8, 1.6f}};
    float gap = std::max(0.28f, 1.7f - wave * 0.06f);
    while (budget > 0) {
        if (wave >= 2 && chance(0.12f)) {
            int n = irnd(4, 7);
            for (int i = 0; i < n; i++) queue.push_back({t + rnd(0, 0.9f), E_RAT, rnd(LANE_TOP, LANE_BOT)});
            budget -= n * 0.8f;
            t += 2.2f;
            continue;
        }
        float total = 0;
        for (auto& o : opts)
            if (o.minW <= wave && o.cost <= budget + 1) total += o.weight;
        float pick = rnd(0, total);
        const Opt* chosen = &opts[0];
        for (auto& o : opts) {
            if (o.minW > wave || o.cost > budget + 1) continue;
            if ((pick -= o.weight) <= 0) { chosen = &o; break; }
        }
        queue.push_back({t, chosen->t, rnd(LANE_TOP, LANE_BOT)});
        budget -= chosen->cost;
        t += gap * rnd(0.5f, 1.4f);
    }
    std::sort(queue.begin(), queue.end(), [](const SpawnEv& a, const SpawnEv& b) { return a.t < b.t; });
}

void Game::startWave() {
    wave++;
    buildWave();
    spawnIdx = 0;
    waveT = 0;
    clearT = -1;
    waveKilled = 0;
    waveTotal = int(queue.size());
    decalClear = true;
    mode = Mode::Play;
    for (int w = 0; w < WEAPON_COUNT; w++) ammo[w] = clipSize(w);
    reloading = jammed = false;
    bool boss = wave % 5 == 0;
    raining = boss || (wave >= 3 && chance(0.4f));
    audio::setRain(raining);
    lightningT = rnd(3, 8);
    if (wave > save.bestWave) {
        if (save.bestWave > 0 && !newBest) toast("NEW RECORD!", 0x8aff8a);
        save.bestWave = wave;
        newBest = true;
        checkUnlocks(true);
        persist();
    }
    if (boss) {
        banner((wave / 5) % 2 == 1 ? "THE RAT KING" : "THE ABOMINATION", "A HORROR APPROACHES...", 0xff4a4a, 3.4f);
        audio::play(SFX_BELL, 0.8f, 0.75f);
    } else {
        banner("WAVE " + std::to_string(wave), WAVE_FLAVOR[wave % 6], 0xffd860);
        audio::play(SFX_BELL, 0.6f);
    }
}

void Game::endWave() {
    for (auto& p : pickups) coins += p.val;
    pickups.clear();
    int bonus = 25 + wave * 12;
    coins += bonus;
    persist();
    toast("WAVE BONUS +" + std::to_string(bonus), 0xffd860);
    mode = Mode::Shop;
    lmb = false;
    audio::play(SFX_BUY, 0.6f);
}

void Game::gameOver() {
    mode = Mode::GameOver;
    timeScale = 1;
    persist();
    checkUnlocks(true);
}

// ============================================================ input

void Game::computeViewport(float& ox, float& oy, float& scale) const {
    int w = VW, h = VH;
    SDL_GetRenderOutputSize(R, &w, &h);
    float s = std::min(w / float(VW), h / float(VH));
    if (s >= 1) s = std::floor(s);
    scale = s;
    ox = std::floor((w - VW * s) / 2);
    oy = std::floor((h - VH * s) / 2);
}

void Game::event(const SDL_Event& e) {
    auto mapMouse = [&](float x, float y) {
        float ox, oy, s;
        computeViewport(ox, oy, s);
        mx = clampf((x - ox) / s, 0, VW - 1);
        my = clampf((y - oy) / s, 0, VH - 1);
    };
    bool canAct = mode == Mode::Play && !dying;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: mapMouse(e.motion.x, e.motion.y); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        mapMouse(e.button.x, e.button.y);
        if (e.button.button == SDL_BUTTON_LEFT) {
            lmb = true;
            clickPending = true;
            if (canAct) {
                int slot = -1;
                for (int i = 0; i < WEAPON_COUNT; i++) {
                    SDL_FRect r = slotRect(i);
                    if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) slot = i;
                }
                if (slot >= 0) {
                    switchWeapon(slot);
                    lmb = false;
                } else {
                    fireQueue = 0.2f;
                }
                clickPending = false;
            }
        } else if (e.button.button == SDL_BUTTON_RIGHT && canAct) {
            throwFlask();
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) lmb = false;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (canAct && e.wheel.y != 0) {
            int dir = e.wheel.y > 0 ? -1 : 1;
            for (int k = 1; k < WEAPON_COUNT; k++) {
                int w = (cur + dir * k + WEAPON_COUNT * 2) % WEAPON_COUNT;
                if (owned[w]) { switchWeapon(w); break; }
            }
        }
        break;
    case SDL_EVENT_KEY_DOWN: {
        if (e.key.repeat) break;
        SDL_Keycode k = e.key.key;
        if (k == SDLK_F11 || (k == SDLK_RETURN && (SDL_GetModState() & SDL_KMOD_ALT))) {
            bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(win, !fs);
            break;
        }
        if (k == SDLK_M) {
            save.muted = !save.muted;
            audio::setMuted(save.muted);
            persist();
            break;
        }
        switch (mode) {
        case Mode::Title:
            if (k == SDLK_RETURN || k == SDLK_SPACE) startRun();
            else if (k == SDLK_LEFT || k == SDLK_A) previewSkin = (previewSkin + SKIN_COUNT - 1) % SKIN_COUNT;
            else if (k == SDLK_RIGHT || k == SDLK_D) previewSkin = (previewSkin + 1) % SKIN_COUNT;
            else if (k == SDLK_ESCAPE) quit = true;
            if (skinUnlocked(previewSkin) && previewSkin != save.skin) {
                save.skin = previewSkin;
                persist();
            }
            break;
        case Mode::Play:
            if (k == SDLK_ESCAPE || k == SDLK_P) mode = Mode::Paused;
            else if (!canAct) break;
            else if (k == SDLK_R || k == SDLK_SPACE) tryActiveReload();
            else if (k >= SDLK_1 && k <= SDLK_5) switchWeapon(int(k - SDLK_1));
            else if (k == SDLK_E) placeTrap();
            else if (k == SDLK_Q) drinkTonic();
            else if (k == SDLK_F) throwFlask();
            break;
        case Mode::Paused:
            if (k == SDLK_ESCAPE || k == SDLK_P) mode = Mode::Play;
            break;
        case Mode::Shop:
            if (k == SDLK_RETURN) startWave();
            break;
        case Mode::GameOver:
            if (k == SDLK_RETURN || k == SDLK_ESCAPE) mode = Mode::Title;
            break;
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        lmb = false;
        if (mode == Mode::Play && !dying) mode = Mode::Paused;
        break;
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET: decalClear = true; break;
    default: break;
    }
}

// ============================================================ frame update

void Game::update(float dt) {
    time += dt;
    float rdt = dt;
    if (mode == Mode::Paused) return;

    timeScale = approach(timeScale, dying ? 0.35f : 1.f, rdt * (dying ? 3.f : 1.2f));
    float wdt = hitstop > 0 ? 0 : rdt * timeScale;
    hitstop = std::max(0.f, hitstop - rdt);
    shake = std::max(0.f, shake - rdt * 22);
    hurtFlash -= rdt;
    bannerT -= rdt;
    coinPulse -= rdt;
    for (auto& t : toasts) t.life -= rdt;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [](const Toast& t) { return t.life <= 0; }), toasts.end());

    updateAmbient(wdt);
    if (mode == Mode::Play) updatePlay(wdt);
    if (mode == Mode::Title) {
        critterT -= rdt;
        if (critterT <= 0) {
            critters.push_back({-12, rnd(LANE_TOP, LANE_BOT), rnd(40, 90), 0});
            critterT = rnd(1.2f, 4.5f);
        }
        for (auto& c : critters) {
            c.x += c.speed * rdt;
            c.anim += rdt * c.speed * 0.13f;
        }
        critters.erase(std::remove_if(critters.begin(), critters.end(), [](const Critter& c) { return c.x > VW + 20; }), critters.end());
    }
    updateParticles(wdt);
}

void Game::updateAmbient(float dt) {
    fogX += dt * 6;
    for (int i = 0; i < 3; i++) {
        cloudX[i] += cloudV[i] * dt;
        if (cloudX[i] > VW + 10) cloudX[i] = -float(A.cloud[i].w) - 10;
    }
    if (raining) {
        float n = dt * 280;
        while (n > 0) {
            if (n >= 1 || chance(n)) rain.push_back({rnd(-70, VW), rnd(-24, -4), rnd(GROUND_Y + 2, VH)});
            n -= 1;
        }
    }
    for (auto& d : rain) {
        d.x += 70 * dt;
        d.y += 400 * dt;
        if (d.y >= d.floorY) {
            for (int k = 0; k < 2; k++) addPart(d.x, d.floorY, rnd(-30, 30), rnd(-50, -20), 0.15f, 0x9ab0d8, 0, 1, 300, d.floorY + 1);
            d.y = 9999;
        }
    }
    rain.erase(std::remove_if(rain.begin(), rain.end(), [](const Drop& d) { return d.y > 9000; }), rain.end());

    if (raining) {
        lightningT -= dt;
        if (lightningT <= 0) {
            flashT = 0.38f;
            thunderT = rnd(0.3f, 1.2f);
            lightningT = rnd(7, 16);
            bolt.clear();
            float x = rnd(20, 360), y = 0, end = rnd(100, 145);
            while (y < end) {
                bolt.push_back({x, y});
                x += rnd(-10, 10);
                y += rnd(6, 14);
            }
            bolt.push_back({x, y});
        }
    }
    flashT -= dt;
    if (thunderT > 0) {
        thunderT -= dt;
        if (thunderT <= 0) audio::play(SFX_THUNDER, 0.8f, rnd(0.8f, 1.1f));
    }
}

void Game::updatePlay(float dt) {
    waveT += dt;
    while (spawnIdx < queue.size() && queue[spawnIdx].t <= waveT) {
        const SpawnEv& s = queue[spawnIdx++];
        spawnEnemy(s.type, -14 - rnd(0, 10) * (isBoss(s.type) ? 3 : 1), s.y, false);
    }
    if (!dying) updateDoctor(dt);
    updateBullets(dt);
    updateEnemies(dt);
    updateMisc(dt);
    for (auto& p : pending) enemies.push_back(p);
    pending.clear();
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.dead || e.x > VW + 40; }),
                  enemies.end());

    if (comboT > 0) {
        comboT -= dt;
        if (comboT <= 0) combo = 0;
    }

    if (dying) {
        deathT += dt;
        if (deathT > 1.1f) gameOver();
        return;
    }
    if (spawnIdx >= queue.size() && enemies.empty()) {
        if (clearT < 0) {
            clearT = 0;
            banner("WAVE CLEARED", "THE CLINIC STANDS", 0x8aff8a, 2.4f);
            audio::play(SFX_PERFECT, 0.6f, 0.9f);
        }
        clearT += dt;
        if (clearT > 2.6f) endWave();
    }
}

// ============================================================ doctor

void Game::shoulder(float& sx, float& sy) const {
    int f = int(time * 1.7f) & 1;
    const Sprite& b = A.doctor[save.skin][f];
    float lx = faceLeft ? DOC_SHOULDER_X : b.w - DOC_SHOULDER_X;
    sx = DOC_X - b.w / 2 + lx - std::cos(aimAng) * recoil;
    sy = DOC_Y - b.h + DOC_SHOULDER_Y + f - std::sin(aimAng) * recoil;
}

void Game::muzzle(float& x, float& y) const {
    float sx, sy;
    shoulder(sx, sy);
    const ArmGfx& a = A.arm[save.skin][cur];
    float lx = a.mx - a.px, ly = a.my - a.py;
    if (faceLeft) ly = -ly;
    float c = std::cos(aimAng), s = std::sin(aimAng);
    x = sx + lx * c - ly * s;
    y = sy + lx * s + ly * c;
}

void Game::updateDoctor(float dt) {
    float sx, sy;
    shoulder(sx, sy);
    float dx = mx - sx, dy = my - sy;
    if (dx * dx + dy * dy > 9) aimAng = std::atan2(dy, dx);
    faceLeft = std::cos(aimAng) < 0;

    const WeaponDef& d = WEAPONS[cur];
    fireCD -= dt * rateMul();
    fireQueue -= dt;
    flameSfxT -= dt;
    recoil = std::max(0.f, recoil - dt * 40);
    muzzleT -= dt;
    crossSpread = std::max(0.f, crossSpread - dt * 25);
    if (reloading) {
        reloadT += dt * rateMul();
        if (reloadT >= reloadDur) {
            reloading = jammed = false;
            ammo[cur] = clipSize(cur);
            audio::play(SFX_RELOAD, 0.5f, 1.2f);
        }
    }
    bool want = d.hold ? lmb : fireQueue > 0;
    if (want && !reloading && fireCD <= 0) {
        if (ammo[cur] > 0) {
            fire();
            fireQueue = 0;
            fireCD = d.interval;
            if (ammo[cur] == 0) startReload();
        } else {
            startReload();
        }
    }
}

void Game::fire() {
    const WeaponDef& d = WEAPONS[cur];
    float x, y;
    muzzle(x, y);
    ammo[cur]--;
    bool perfect = blessedShots > 0;
    if (perfect) blessedShots--;
    float dmg = d.dmg * dmgMul() * (perfect ? 1.4f : 1.f);
    for (int i = 0; i < d.pellets; i++) {
        float a = aimAng + (d.flame ? (rnd() - rnd()) * d.spread * 1.5f : rnd(-d.spread, d.spread));
        float sp = d.speed * (d.flame ? rnd(0.7f, 1.2f) : rnd(0.92f, 1.06f));
        Bullet b{};
        b.x = x;
        b.y = y;
        b.vx = std::cos(a) * sp;
        b.vy = std::sin(a) * sp;
        b.dmg = dmg;
        b.life = b.maxLife = d.flame ? rnd(0.35f, 0.5f) : 1.4f;
        b.blast = d.blast;
        b.knock = d.knock;
        b.pierce = d.pierce;
        b.flame = d.flame;
        b.perfect = perfect;
        b.col = perfect ? 0xffe060 : d.col;
        bullets.push_back(b);
    }
    muzzleT = d.flame ? 0.03f : 0.07f;
    recoil = d.flame ? 0.5f : std::min(4.f, d.knock * 0.04f);
    crossSpread = std::min(7.f, crossSpread + (d.flame ? 0.4f : 2 + d.shake));
    addShake(d.shake);
    float c = std::cos(aimAng), s = std::sin(aimAng);
    if (!d.flame) {
        for (int k = 0; k < 4; k++)
            addPart(x, y, c * rnd(10, 35) + rnd(-6, 6), s * rnd(10, 35) - rnd(4, 14), rnd(0.5f, 1.0f), 0x8a8a9a, 1, rnd(3, 6), -12, 999, 2);
        for (int k = 0; k < 6; k++) {
            float a = aimAng + rnd(-0.5f, 0.5f), sp = rnd(80, 180);
            addPart(x, y, std::cos(a) * sp, std::sin(a) * sp, rnd(0.08f, 0.18f), 0xffd070, 3);
        }
        if (cur != 2) {
            float sx, sy;
            shoulder(sx, sy);
            addPart(sx, sy, -c * rnd(20, 40) + rnd(-10, 10), -rnd(60, 100), 1.6f, 0xd8b048, 5, 1, 420, DOC_Y - rnd(0, 3));
        }
        audio::play(d.sfx, perfect ? 0.85f : 0.7f, rnd(0.92f, 1.08f) * (perfect ? 0.9f : 1.f));
        tlights.push_back({x, y, 80, 0.08f, 0.08f, 0xffc070});
    } else if (flameSfxT <= 0) {
        audio::play(SFX_FLAME, 0.4f, rnd(0.85f, 1.1f));
        flameSfxT = 0.09f;
    }
}

void Game::startReload() {
    if (reloading || ammo[cur] >= clipSize(cur)) return;
    reloading = true;
    jammed = false;
    reloadT = 0;
    reloadDur = WEAPONS[cur].reload;
    audio::play(SFX_RELOAD, 0.4f, 0.8f);
}

void Game::tryActiveReload() {
    if (!reloading) {
        startReload();
        return;
    }
    if (jammed) return;
    float p = reloadT / reloadDur;
    float lo = save.skin == 5 ? 0.36f : 0.42f, hi = save.skin == 5 ? 0.68f : 0.60f;
    float sx, sy;
    shoulder(sx, sy);
    if (p >= lo && p <= hi) {
        reloading = false;
        ammo[cur] = clipSize(cur);
        blessedShots = clipSize(cur);
        popup("PERFECT!", DOC_X, DOC_Y - 46, 0xffe060, 1, 1.0f);
        audio::play(SFX_PERFECT, 0.7f);
        for (int k = 0; k < 16; k++) {
            float a = rnd(0, 2 * PI), sp = rnd(30, 90);
            addPart(sx, sy, std::cos(a) * sp, std::sin(a) * sp, rnd(0.3f, 0.6f), 0xffe060, 6, 1, 0, 999, 3);
        }
    } else {
        jammed = true;
        reloadDur += 0.6f;
        popup("JAMMED!", DOC_X, DOC_Y - 46, 0xff5040, 1, 0.9f);
        audio::play(SFX_JAM, 0.6f);
    }
}

void Game::throwFlask() {
    if (flasks <= 0) {
        popup("NO FLASKS", DOC_X - 10, DOC_Y - 40, 0xff8060);
        audio::play(SFX_EMPTY, 0.5f);
        return;
    }
    flasks--;
    float sx, sy;
    shoulder(sx, sy);
    float tx = clampf(mx, 8, DOC_X - 24), ty = clampf(my, LANE_TOP, LANE_BOT);
    float dist = std::hypot(tx - sx, ty - sy);
    flaskList.push_back({sx, sy - 4, tx, ty, 0, 0.3f + dist / 480.f});
    audio::play(SFX_THROW, 0.6f);
}

void Game::placeTrap() {
    if (traps <= 0) {
        popup("NO TRAPS", DOC_X - 10, DOC_Y - 40, 0xff8060);
        audio::play(SFX_EMPTY, 0.5f);
        return;
    }
    traps--;
    float tx = clampf(mx, 16, BARR_X - 16), ty = clampf(my, LANE_TOP, LANE_BOT);
    trapList.push_back({tx, ty, 0, false});
    audio::play(SFX_TRAP, 0.3f, 1.6f);
    for (int k = 0; k < 6; k++) addPart(tx + rnd(-6, 6), ty, rnd(-20, 20), rnd(-40, -10), 0.4f, 0x8a8478, 0, 1, 200, ty + 1);
}

void Game::drinkTonic() {
    if (tonics <= 0) {
        popup("NO TONICS", DOC_X - 10, DOC_Y - 40, 0xff8060);
        audio::play(SFX_EMPTY, 0.5f);
        return;
    }
    if (hp >= maxHp) {
        popup("ALREADY HEALTHY", DOC_X - 10, DOC_Y - 40, 0xa0ffa0);
        return;
    }
    tonics--;
    float heal = std::min(45.f, maxHp - hp);
    hp += heal;
    popup("+" + std::to_string(int(heal)), DOC_X, DOC_Y - 40, 0x60ff80, 1, 1.0f);
    audio::play(SFX_DRINK, 0.7f);
    for (int k = 0; k < 18; k++)
        addPart(DOC_X + rnd(-8, 8), DOC_Y - rnd(0, 26), rnd(-8, 8), rnd(-50, -20), rnd(0.5f, 1.0f), 0x60ff80, 6);
}

void Game::switchWeapon(int w) {
    if (w < 0 || w >= WEAPON_COUNT || !owned[w] || w == cur) return;
    cur = w;
    reloading = jammed = false;
    fireCD = 0.12f;
    audio::play(SFX_CLICK, 0.5f);
    if (ammo[cur] == 0) startReload();
}

// ============================================================ enemies

SDL_FRect Game::enemyBox(const Enemy& e) const {
    const EnemyDef& d = EDEF[e.t];
    float w = d.w * d.scale, h = d.h * d.scale;
    if (d.fly) return {e.x - w / 2, e.y - h / 2, w, h};
    return {e.x - w / 2, e.y - h, w, h};
}

Enemy& Game::spawnEnemy(EType t, float x, float y, bool deferred) {
    const EnemyDef& d = EDEF[t];
    Enemy e;
    e.id = nextId++;
    e.t = t;
    float hpMul = 1 + 0.12f * (wave - 1) + (wave > 12 ? 0.08f * (wave - 12) : 0.f);
    e.hp = e.maxHp = d.hp * hpMul;
    e.speed = d.speed * rnd(0.88f, 1.12f) * (1 + std::min(0.35f, wave * 0.012f));
    e.dmg = d.dmg * (1 + 0.05f * (wave - 1));
    e.atkT = d.atkInt * 0.5f;
    e.x = x;
    e.y = y;
    e.anim = rnd(0, 10);
    if (t == E_CROW) {
        e.y = e.hoverY = rnd(30, 90);
        e.standX = rnd(150, 260);
    }
    if (t == E_SPITTER) e.standX = rnd(110, 190);
    auto& vec = deferred ? pending : enemies;
    vec.push_back(e);
    if (deferred) waveTotal++;
    if (isBoss(t)) audio::play(SFX_GROAN, 0.9f, 0.5f);
    return vec.back();
}

void Game::damageEnemy(Enemy& e, float dmg, bool crit, float knock, bool showPopup) {
    if (e.dead) return;
    const EnemyDef& d = EDEF[e.t];
    e.hp -= dmg;
    e.flash = 0.07f;
    e.knock += knock * (1 - d.knockRes);
    if (showPopup) {
        SDL_FRect b = enemyBox(e);
        if (crit) popup(std::to_string(int(dmg)) + "!", e.x, b.y - 6, 0xffe040, 1, 0.8f);
        else popup(std::to_string(int(dmg)), e.x + rnd(-4, 4), b.y - 3, 0xf0f0f0, 1, 0.5f);
    }
    if (e.hp <= 0) killEnemy(e, crit);
}

void Game::killEnemy(Enemy& e, bool crit) {
    if (e.dead) return;
    e.dead = true;
    const EnemyDef& d = EDEF[e.t];
    waveKilled++;
    runKills++;
    save.totalKills++;
    combo++;
    comboT = 2.2f;
    bestCombo = std::max(bestCombo, combo);
    if (combo >= 5 && combo % 5 == 0) audio::play(SFX_COIN, 0.4f, 1.5f + std::min(combo, 30) * 0.02f);

    SDL_FRect b = enemyBox(e);
    float cy = b.y + b.h / 2;
    float comboMul = 1 + std::min(combo, 20) * 0.05f;
    int val = std::max(1, int(std::round(d.coins * coinMul() * comboMul)));
    spawnCoins(e.x, cy, val);
    gibs(e);

    switch (e.t) {
    case E_RAT:
    case E_PLAGUERAT: audio::play(SFX_SQUEAK, 0.5f, rnd(0.9f, 1.2f)); break;
    case E_RATLING: audio::play(SFX_SQUEAK, 0.35f, rnd(1.3f, 1.6f)); break;
    case E_GIANTRAT: audio::play(SFX_SQUEAK, 0.7f, 0.6f); break;
    case E_RATKING: audio::play(SFX_SQUEAK, 1.0f, 0.4f); break;
    case E_CROW: audio::play(SFX_CAW, 0.5f, 1.3f); break;
    default:
        audio::play(SFX_SPLAT, 0.7f, rnd(0.8f, 1.1f));
        audio::play(SFX_GROAN, 0.4f, rnd(0.6f, 0.8f));
        break;
    }

    if (e.t == E_PLAGUERAT) {
        for (int k = 0; k < 3; k++)
            spawnEnemy(E_RATLING, e.x + rnd(-6, 6), clampf(e.y + rnd(-7, 7), LANE_TOP, LANE_BOT), true);
        for (int k = 0; k < 8; k++) addPart(e.x, cy, rnd(-30, 30), rnd(-30, 5), rnd(0.6f, 1.2f), 0x7aa040, 1, rnd(3, 6), -10, 999, 2);
    }
    if (e.t == E_BLOATER || e.t == E_ABOM) {
        float r = e.t == E_ABOM ? 70.f : 44.f;
        explode(e.x, cy, r, (e.t == E_ABOM ? 150.f : 70.f) * (1 + 0.05f * wave), e.t == E_ABOM);
        for (int k = 0; k < 14; k++)
            addPart(e.x + rnd(-r / 2, r / 2), cy + rnd(-8, 8), rnd(-25, 25), rnd(-30, 0), rnd(1.2f, 2.4f), 0x6a8a3a, 1, rnd(5, 11), -6, 999, 1.5f);
    }
    if (isBoss(e.t)) {
        save.bossKills++;
        bossesThisRun++;
        timeScale = 0.2f;
        hitstop = std::max(hitstop, 0.12f);
        addShake(8);
        banner("BOSS SLAIN!", std::string(d.name) + " IS NO MORE", 0xffd860, 3.f);
        for (int k = 0; k < 6; k++) spawnCoins(e.x + rnd(-20, 20), cy, int(val / 6) + 1);
    } else if (d.scale >= 2 || e.t == E_BRUTE) {
        hitstop = std::max(hitstop, 0.05f);
        addShake(2);
    } else if (crit) {
        hitstop = std::max(hitstop, 0.02f);
    }
    checkUnlocks(true);
}

void Game::explode(float x, float y, float r, float dmg, bool big) {
    for (auto& e : enemies) {
        if (e.dead) continue;
        SDL_FRect b = enemyBox(e);
        float ex = b.x + b.w / 2, ey = b.y + b.h / 2;
        float dd = std::hypot(ex - x, (ey - y) * 1.3f);
        if (dd < r + b.w * 0.4f) {
            float f = 1 - 0.5f * clampf(dd / r, 0, 1);
            damageEnemy(e, dmg * f, false, ex < x ? 140.f : -140.f, true);
            if (!e.dead) e.burn = std::max(e.burn, 1.0f);
        }
    }
    for (auto& g : globs)
        if (std::hypot(g.x - x, g.y - y) < r) g.dead = true;
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
    hitstop = std::max(hitstop, 0.035f);
    audio::play(SFX_EXPLODE, big ? 0.95f : 0.7f, rnd(0.9f, 1.1f) * (big ? 0.8f : 1.f));
}

void Game::hitBarricade(float d) {
    if (barrHp <= 0) return;
    barrHp -= d;
    addShake(0.8f);
    audio::play(SFX_THUD, 0.5f, rnd(0.8f, 1.2f));
    uint32_t wood = barrTier == 2 ? 0x8a8f99 : 0x8a5a34;
    for (int k = 0; k < 5; k++) {
        float fy = rnd(LANE_TOP, LANE_BOT);
        addPart(BARR_X + rnd(-3, 3), fy - rnd(4, 18), rnd(-70, 20), rnd(-110, -30), rnd(1, 2), wood, 2, 1, 400, fy);
    }
    if (barrHp <= 0) {
        barrHp = 0;
        banner("BARRICADE BROKEN!", "THEY ARE COMING FOR YOU", 0xff4a4a, 2.2f);
        audio::play(SFX_BREAK, 0.9f);
        addShake(6);
        for (int k = 0; k < 40; k++) {
            float fy = rnd(LANE_TOP, LANE_BOT);
            addPart(BARR_X + rnd(-4, 4), fy - rnd(0, 24), rnd(-120, 80), rnd(-180, -40), rnd(2, 4), k % 3 ? wood : 0x48291a, 2,
                    rnd(1, 2), 420, fy);
        }
    }
}

void Game::hurtDoctor(float d) {
    if (dying || mode != Mode::Play) return;
    hp -= d;
    hurtFlash = 0.4f;
    addShake(3);
    combo = 0;
    comboT = 0;
    audio::play(SFX_HURT, 0.7f, rnd(0.9f, 1.1f));
    popup("-" + std::to_string(int(std::ceil(d))), DOC_X, DOC_Y - 36, 0xff4040, 1, 0.9f);
    blood(DOC_X, DOC_Y - 16, DOC_Y, 0x8a1020, 8, 1);
    if (hp <= 0) {
        hp = 0;
        dying = true;
        deathT = 0;
        reloading = false;
        lmb = false;
        audio::play(SFX_BELL, 0.9f, 0.6f);
        banner("THE DOCTOR HAS FALLEN", "", 0xff4040, 3.f);
    }
}

void Game::spit(float x, float y, float flight) {
    float tx = DOC_X + rnd(-3, 3), ty = DOC_Y - 14;
    const float g = 200;
    globs.push_back({x, y, (tx - x) / flight, (ty - y) / flight - 0.5f * g * flight, false});
    audio::play(SFX_SPIT, 0.5f, rnd(0.8f, 1.2f));
}

void Game::updateEnemies(float dt) {
    for (size_t i = 0; i < enemies.size(); i++) {
        Enemy& e = enemies[i];
        if (e.dead) continue;
        const EnemyDef& d = EDEF[e.t];
        e.flash -= dt;
        e.lunge = std::max(0.f, e.lunge - dt * 5);
        if (e.burn > 0) {
            e.burn -= dt;
            e.hp -= 10 * dt * (1 + 0.05f * wave);
            if (chance(dt * 30)) {
                SDL_FRect b = enemyBox(e);
                addPart(rnd(b.x, b.x + b.w), rnd(b.y, b.y + b.h), rnd(-8, 8), rnd(-50, -20), rnd(0.2f, 0.45f), 0xff9030, 4, rnd(3, 6), -30);
            }
            if (e.hp <= 0) {
                killEnemy(e, false);
                continue;
            }
        }
        if (e.knock != 0) {
            e.x -= e.knock * dt;
            e.knock = approach(e.knock, 0, 520 * dt);
        }
        if (d.fly) {
            updateCrow(e, dt);
            continue;
        }
        if (e.trapT > 0) {
            e.trapT -= dt;
            continue;
        }
        float halfW = d.w * d.scale * 0.5f;
        float stop = barrHp > 0 ? BARR_X - 6 - halfW : DOC_X - 12 - halfW;
        e.attacking = false;
        if (e.t == E_SPITTER && e.x >= e.standX) {
            e.attacking = true;
            e.atkT -= dt;
            if (e.atkT <= 0) {
                e.atkT = d.atkInt * rnd(0.8f, 1.2f);
                e.lunge = 1;
                spit(e.x + 6, e.y - 14, rnd(1.0f, 1.25f));
            }
        } else if (e.x < stop) {
            e.x += e.speed * dt;
            e.anim += dt * (e.speed * 0.13f + 2);
        } else {
            e.x = stop;
            e.attacking = true;
            if (e.t == E_BLOATER) {
                if (barrHp > 0) hitBarricade(35 + wave * 2.f);
                else hurtDoctor(20 + wave * 0.5f);
                e.dead = true;
                waveKilled++;
                explode(e.x, e.y - 9, 40, 50, false);
                continue;
            }
            e.atkT -= dt;
            if (e.atkT <= 0) {
                e.atkT = d.atkInt;
                e.lunge = 1;
                if (barrHp > 0) hitBarricade(e.dmg);
                else hurtDoctor(e.dmg);
            }
        }
        if (e.t == E_RATKING) {
            e.stT += dt;
            if (e.stT > 5.5f) {
                e.stT = 0;
                for (int k = 0; k < 3; k++) spawnEnemy(E_RAT, e.x - rnd(4, 24), clampf(e.y + rnd(-12, 12), LANE_TOP, LANE_BOT), true);
                audio::play(SFX_SQUEAK, 0.8f, 0.55f);
                popup("RISE, MY CHILDREN!", e.x, e.y - 34, 0xd0a0ff, 1, 1.4f);
                for (int k = 0; k < 10; k++) addPart(e.x, e.y - 8, rnd(-40, 40), rnd(-40, 0), rnd(0.5f, 1), 0x6a4a8a, 1, rnd(4, 8), -10, 999, 2);
            }
        }
        if (e.t == E_ABOM) {
            e.stT += dt;
            e.stT2 += dt;
            if (e.stT > 8) {
                e.stT = 0;
                spawnEnemy(E_BLOATER, e.x - 14, clampf(e.y + rnd(-10, 10), LANE_TOP, LANE_BOT), true);
            }
            if (e.stT2 > 4.5f) {
                e.stT2 = 0;
                for (int k = 0; k < 3; k++) spit(e.x - 2, e.y - 32, 0.9f + k * 0.22f);
            }
        }
        bool mutant = e.t == E_BLOATER || e.t == E_BRUTE || e.t == E_SPITTER || e.t == E_ABOM;
        if (mutant && chance(dt * 0.08f)) audio::play(SFX_GROAN, 0.25f, rnd(0.7f, 1.1f) * (e.t == E_ABOM ? 0.5f : 1.f));
        if ((e.t == E_BLOATER || e.t == E_ABOM) && chance(dt * 3)) {
            SDL_FRect b = enemyBox(e);
            addPart(rnd(b.x, b.x + b.w), rnd(b.y, b.y + b.h * 0.7f), rnd(-5, 5), rnd(-20, -8), rnd(0.6f, 1.2f), 0xb0ff50, 6);
        }
    }
}

void Game::updateCrow(Enemy& e, float dt) {
    e.anim += dt * 12;
    switch (e.state) {
    case 0: {
        float tx = e.standX, ty = e.hoverY + std::sin(time * 3 + e.id) * 6;
        float dx = tx - e.x, dy = ty - e.y, dist = std::hypot(dx, dy);
        if (dist > 0.01f) {
            e.vx = dx / dist * e.speed;
            e.vy = dy / dist * e.speed;
        }
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        if (dist < 6) {
            e.state = 1;
            e.stT = 0.6f;
            audio::play(SFX_CAW, 0.4f, rnd(0.9f, 1.2f));
        }
        break;
    }
    case 1:
        e.stT -= dt;
        e.vx = 10;
        e.y += std::sin(time * 30) * 0.3f;
        if (e.stT <= 0) {
            e.state = 2;
            float dx = DOC_X - e.x, dy = DOC_Y - 22 - e.y, dd = std::max(1.f, std::hypot(dx, dy));
            float sp = 150 + wave * 3.f;
            e.vx = dx / dd * sp;
            e.vy = dy / dd * sp;
        }
        break;
    case 2:
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        if (std::hypot(e.x - DOC_X, e.y - (DOC_Y - 20)) < 9 || e.x > VW + 10 || e.y > VH) {
            if (e.x < VW) hurtDoctor(e.dmg);
            e.state = 3;
            e.stT = 1.2f;
            e.vx = -90;
            e.vy = -110;
        }
        break;
    case 3:
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        e.vy += 50 * dt;
        e.stT -= dt;
        if (e.stT <= 0) {
            e.state = 0;
            e.standX = rnd(130, 260);
            e.hoverY = rnd(30, 90);
        }
        break;
    }
}

// ============================================================ projectiles & misc

void Game::updateBullets(float dt) {
    for (auto& b : bullets) {
        if (b.dead) continue;
        b.life -= dt;
        if (b.life <= 0) { b.dead = true; continue; }
        if (b.flame) {
            float k = std::max(0.f, 1 - 2.2f * dt);
            b.vx *= k;
            b.vy = b.vy * k - 25 * dt;
        }
        float len = std::hypot(b.vx, b.vy) * dt;
        int steps = std::max(1, int(len / 3) + 1);
        for (int s = 0; s < steps && !b.dead; s++) {
            b.x += b.vx * dt / steps;
            b.y += b.vy * dt / steps;
            if (!b.flame) {
                for (auto& g : globs) {
                    if (g.dead || std::fabs(g.x - b.x) > 4 || std::fabs(g.y - b.y) > 4) continue;
                    g.dead = true;
                    b.dead = true;
                    coins += 1;
                    popup("+1", g.x, g.y - 6, 0xffd860);
                    for (int k = 0; k < 8; k++) addPart(g.x, g.y, rnd(-60, 60), rnd(-60, 20), rnd(0.2f, 0.4f), 0xa8ff3a, 6, 1, 200);
                    audio::play(SFX_SPLAT, 0.3f, 1.6f);
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
                if (b.flame) {
                    e.burn = std::max(e.burn, 2.6f);
                    damageEnemy(e, b.dmg, false, 0, false);
                    b.dmg *= 0.7f;
                    continue;
                }
                bool crit = b.y < bx.y + bx.h * 0.34f;
                float dmg = b.dmg * (crit ? critMul() : 1.f);
                blood(b.x, b.y, e.y, EDEF[e.t].blood, crit ? 8 : 4, b.vx);
                if (crit)
                    for (int k = 0; k < 5; k++)
                        addPart(b.x, b.y, rnd(-80, 80), rnd(-120, -20), rnd(0.1f, 0.25f), 0xfff0a0, 3, 1, 200);
                damageEnemy(e, dmg, crit, b.vx < 0 ? b.knock : -b.knock * 0.3f, true);
                if (b.blast > 0) {
                    explode(b.x, b.y, b.blast, b.dmg * 0.8f, false);
                    b.dead = true;
                    break;
                }
                if (--b.pierce < 0) { b.dead = true; break; }
                b.dmg *= 0.8f;
            }
        }
        if (b.dead) continue;
        if (b.x < -30 || b.x > VW + 30 || b.y < -60) b.dead = true;
        else if (!b.flame && b.y > LANE_BOT + 6) {
            b.dead = true;
            for (int k = 0; k < 4; k++) addPart(b.x, b.y, rnd(-30, 30), rnd(-70, -20), 0.35f, 0x6a6474, 0, 1, 300, b.y + 1);
            if (b.blast > 0) explode(b.x, LANE_BOT, b.blast, b.dmg * 0.8f, false);
        }
    }
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return b.dead; }), bullets.end());
}

void Game::updateMisc(float dt) {
    // Flasks in flight
    for (auto& f : flaskList) {
        f.t += dt;
        if (chance(dt * 40)) {
            float u = f.t / f.dur;
            float x = lerp(f.x0, f.x1, u), y = lerp(f.y0, f.y1, u) - std::sin(PI * u) * 34;
            addPart(x, y, rnd(-5, 5), rnd(-15, 0), 0.25f, 0xff8030, 4, 3, -20);
        }
        if (f.t >= f.dur) {
            explode(f.x1, f.y1, 40, 70 * dmgMul(), true);
            for (int k = 0; k < 3; k++) fires.push_back({f.x1 + rnd(-14, 14), clampf(f.y1 + rnd(-6, 6), LANE_TOP, LANE_BOT), rnd(3, 4.5f)});
            for (int k = 0; k < 10; k++) addPart(f.x1, f.y1 - 2, rnd(-70, 70), rnd(-120, -40), rnd(0.6f, 1.2f), 0xb0d8e8, 2, 1, 400, f.y1 + rnd(-2, 3));
        }
    }
    flaskList.erase(std::remove_if(flaskList.begin(), flaskList.end(), [](const Flask& f) { return f.t >= f.dur; }), flaskList.end());

    // Traps
    for (auto& tr : trapList) {
        if (tr.shut) { tr.t += dt; continue; }
        for (auto& e : enemies) {
            const EnemyDef& d = EDEF[e.t];
            if (e.dead || d.fly) continue;
            if (std::fabs(e.x - tr.x) > 7 + d.w * d.scale * 0.25f || std::fabs(e.y - tr.y) > 8) continue;
            tr.shut = true;
            e.trapT = isBoss(e.t) ? 0.8f : (e.t == E_BRUTE ? 1.8f : 3.2f);
            audio::play(SFX_TRAP, 0.7f);
            for (int k = 0; k < 8; k++) addPart(tr.x, tr.y - 3, rnd(-90, 90), rnd(-120, -30), rnd(0.1f, 0.25f), 0xfff0a0, 3, 1, 200);
            damageEnemy(e, 45 * dmgMul(), false, 0, true);
            break;
        }
    }
    trapList.erase(std::remove_if(trapList.begin(), trapList.end(), [](const Trap& t) { return t.shut && t.t > 3.5f; }), trapList.end());

    // Ground fires
    for (auto& f : fires) {
        f.life -= dt;
        if (chance(dt * 22)) addPart(f.x + rnd(-6, 6), f.y - rnd(0, 3), rnd(-6, 6), rnd(-45, -20), rnd(0.25f, 0.5f), 0xff9030, 4, rnd(3, 7), -30);
        for (auto& e : enemies)
            if (!e.dead && !EDEF[e.t].fly && std::fabs(e.x - f.x) < 12 && std::fabs(e.y - f.y) < 7) e.burn = std::max(e.burn, 1.2f);
    }
    fires.erase(std::remove_if(fires.begin(), fires.end(), [](const FirePatch& f) { return f.life <= 0; }), fires.end());

    // Acid globs
    for (auto& g : globs) {
        if (g.dead) continue;
        g.vy += 200 * dt;
        g.x += g.vx * dt;
        g.y += g.vy * dt;
        if (chance(dt * 30)) addPart(g.x, g.y, rnd(-5, 5), rnd(-5, 5), 0.3f, 0x8ae030, 6);
        if (std::fabs(g.x - DOC_X) < 7 && g.y > DOC_Y - 28 && g.y < DOC_Y) {
            g.dead = true;
            hurtDoctor(9 + wave * 0.4f);
            for (int k = 0; k < 10; k++) addPart(g.x, g.y, rnd(-60, 60), rnd(-80, 0), rnd(0.2f, 0.5f), 0xa8ff3a, 6, 1, 250);
        } else if (g.y > LANE_BOT + 4 || g.x > VW) {
            g.dead = true;
            decals.push_back({g.x, g.y, 2, 0x4a7a1a});
            for (int k = 0; k < 6; k++) addPart(g.x, g.y, rnd(-40, 40), rnd(-50, -10), rnd(0.2f, 0.4f), 0xa8ff3a, 6, 1, 250);
        }
    }
    globs.erase(std::remove_if(globs.begin(), globs.end(), [](const Glob& g) { return g.dead; }), globs.end());
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
        } else {
            float dx = HUD_COIN_X - p.x, dy = HUD_COIN_Y - p.y, d = std::hypot(dx, dy);
            float sp = 120 + (p.t - 0.75f) * 900;
            if (d < sp * dt + 3) {
                coins += p.val;
                p.val = -1;
                coinPulse = 0.15f;
                audio::play(SFX_COIN, 0.22f, coinPitch);
                coinPitch = std::min(2.2f, coinPitch + 0.05f);
                coinPitchT = 0.4f;
            } else {
                p.x += dx / d * sp * dt;
                p.y += dy / d * sp * dt;
            }
        }
    }
    pickups.erase(std::remove_if(pickups.begin(), pickups.end(), [](const Pickup& p) { return p.val < 0; }), pickups.end());
}

// ============================================================ fx helpers

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
    case E_CROW: chunk = 0x1a1824; break;
    case E_BLOATER:
    case E_SPITTER:
    case E_BRUTE: chunk = 0x7d8f5a; break;
    case E_ABOM: chunk = 0x9a5a5a; break;
    default: break;
    }
    int n = std::min(48, int(8 * d.scale * d.scale + (d.h > 10 ? 8 : 0)));
    for (int k = 0; k < n; k++) {
        float size = (d.scale >= 2 && chance(0.5f)) ? 2.f : 1.f;
        addPart(rnd(b.x, b.x + b.w), rnd(b.y, b.y + b.h), rnd(-80, 80), rnd(-170, -40), rnd(1.6f, 3.2f), k % 2 ? chunk : d.blood, 2,
                size, 400, floorY + rnd(-2, 3));
    }
    if (e.t == E_CROW)
        for (int k = 0; k < 10; k++)
            addPart(e.x, e.y, rnd(-40, 40), rnd(-60, 10), rnd(1.5f, 2.5f), 0x2a2a44, 0, 1, 25, floorY, 1.5f);
    for (int k = 0; k < int(3 * d.scale); k++)
        decals.push_back({e.x + rnd(-b.w / 2, b.w / 2), floorY + rnd(-2, 2), rnd(1.5f, 3.f) * d.scale, darker(d.blood)});
}

void Game::popup(const std::string& s, float x, float y, uint32_t col, int scale, float life) {
    popups.push_back({s, x, y, life, life, col, scale});
}

void Game::banner(const std::string& t, const std::string& sub, uint32_t col, float dur) {
    bannerText = t;
    bannerSub = sub;
    bannerCol = col;
    bannerT = bannerMax = dur;
}

void Game::toast(const std::string& s, uint32_t col) { toasts.push_back({s, 3.2f, col}); }

void Game::spawnCoins(float x, float y, int val) {
    int n = std::clamp(val / 3, 1, 6);
    int each = val / n, rem = val - each * n;
    for (int i = 0; i < n; i++)
        pickups.push_back({x, y, rnd(-45, 45), rnd(-110, -60), rnd(0, 0.15f), clampf(y + rnd(8, 20), LANE_TOP, LANE_BOT + 4),
                           each + (i == 0 ? rem : 0)});
}
