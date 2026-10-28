// Run flow, waves, input and the doctors themselves.
#include "Defs.hpp"
#include "Net.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace util;

namespace {
const char* WAVE_FLAVOR[] = {"THE VERMIN STIR",    "THE MIASMA THICKENS", "HOLD THE LINE, DOCTORS",
                             "THEY SMELL MEDICINE", "PRAY FOR DAWN",       "THE BELLS TOLL FOR THEE"};
constexpr float BLEED_OUT = 30.f, REVIVE_TIME = 2.5f, WALK_SPEED = 62.f;
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
    ipText = save.lastIp;
    audio::setMuted(save.muted);
    applyVolumes();
    audio::setMusicTrack(0);
    audio::setMusic(true);
    // The title screen shows a random stage the player has already reached.
    int unlocked = 0;
    for (int s = 0; s < STAGE_COUNT; s++)
        if (stageUnlocked(s)) unlocked = s;
    titleStage = irnd(0, unlocked);
    raining = titleStage == 0 && chance(0.5f);
    syncStageAudio();
    resetPlayer(players[0], 0);
    players[0].name = net::localName();
    SDL_HideCursor();
    return true;
}

void Game::shutdown() {
    if (online()) net::leave();
    persist();
    for (SDL_Texture* t : {sceneTex, lightTex, decalTex, entityTex})
        if (t) SDL_DestroyTexture(t);
}

void Game::load() {
    if (savePath.empty()) return;
    std::ifstream f(savePath);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        std::string key, val;
        if (!(ls >> key >> val)) continue;
        if (key == "ip") { save.lastIp = val; continue; }
        int v = std::atoi(val.c_str());
        if (key == "best") save.bestWave = v;
        else if (key == "kills") save.totalKills = v;
        else if (key == "bosses") save.bossKills = v;
        else if (key == "skin") save.skin = std::clamp(v, 0, SKIN_COUNT - 1);
        else if (key == "muted") save.muted = v != 0;
        else if (key == "runs") save.runs = v;
        else if (key == "coop") save.coopRuns = v;
        else if (key == "revives") save.revives = v;
        else if (key == "combo") save.bestCombo = v;
        else if (key == "stages") save.stageClears = v;
        else if (key == "shake") save.shake = v != 0;
        else if (key == "dmgnum") save.dmgNumbers = v != 0;
        else if (key == "vmaster") save.volMaster = std::clamp(v, 0, 10);
        else if (key == "vmusic") save.volMusic = std::clamp(v, 0, 10);
        else if (key == "vsfx") save.volSfx = std::clamp(v, 0, 10);
    }
}

void Game::persist() {
    if (savePath.empty()) return;
    std::ofstream f(savePath);
    f << "best " << save.bestWave << "\nkills " << save.totalKills << "\nbosses " << save.bossKills << "\nskin " << save.skin
      << "\nmuted " << (save.muted ? 1 : 0) << "\nruns " << save.runs << "\ncoop " << save.coopRuns << "\nrevives " << save.revives
      << "\ncombo " << save.bestCombo << "\nstages " << save.stageClears << "\nshake " << (save.shake ? 1 : 0) << "\ndmgnum "
      << (save.dmgNumbers ? 1 : 0) << "\nvmaster " << save.volMaster << "\nvmusic " << save.volMusic << "\nvsfx " << save.volSfx
      << "\nip " << (save.lastIp.empty() ? "127.0.0.1" : save.lastIp) << "\n";
}

void Game::applyVolumes() { audio::setVolumes(save.volMaster / 10.f, save.volMusic / 10.f, save.volSfx / 10.f); }

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

bool Game::stageUnlocked(int s) const { return s == 0 || save.bestWave >= s * 5 + 1; }

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

int Game::activeCount() const {
    int n = 0;
    for (auto& p : players) n += p.active;
    return n;
}
int Game::aliveCount() const {
    int n = 0;
    for (auto& p : players) n += p.alive();
    return n;
}
int Game::clipSize(const Player& p, int w) const { return std::max(1, int(std::round(WEAPONS[w].clip * (1 + 0.34f * p.up[2])))); }
float Game::dmgMul(const Player& p) const {
    return (1 + 0.15f * p.up[0]) * (p.skin == 6 ? 1.1f : 1.f) * (lastStand && p.alive() ? 1.25f : 1.f);
}
float Game::coinMul(const Player& p) const { return p.skin == 4 ? 1.15f : (p.skin == 6 ? 1.1f : 1.f); }
float Game::critMul(const Player& p) const { return p.skin == 1 ? 2.5f : 2.f; }
float Game::rateMul(const Player& p) const { return (1 + 0.12f * p.up[1]) * (lastStand && p.alive() ? 1.25f : 1.f); }
float Game::barrMaxFor(int tier) const {
    const float base[3] = {150, 260, 400};
    bool ashen = false;
    for (auto& p : players) ashen |= p.active && p.skin == 3;
    return base[std::clamp(tier, 0, 2)] * (ashen ? 1.3f : 1.f) * (1 + 0.2f * (activeCount() - 1));
}
int Game::upgradeCost(const Player& p, int i) const { return UPGRADES[i].base * ((i == 4 ? barrTier : p.up[i]) + 1); }
// Enemy toughness grows with the number of doctors defending.
float Game::hpScale() const { return 1 + 0.35f * (std::max(1, activeCount()) - 1); }
int Game::stageForWave(int w) const { return ((std::max(1, w) - 1) / 5) % STAGE_COUNT; }

// ============================================================ run flow

void Game::resetPlayer(Player& p, int slot) {
    uint64_t peer = p.peer;
    std::string name = p.name;
    int skin = p.skin;
    bool active = p.active;
    p = Player{};
    p.peer = peer;
    p.name = name;
    p.skin = skin;
    p.active = active;
    p.x = SPAWN_X[slot];
    p.y = SPAWN_Y[slot];
    for (int w = 0; w < WEAPON_COUNT; w++) {
        p.owned[w] = w == 0;
        p.ammo[w] = clipSize(p, w);
    }
    p.flasks = 2 + (p.skin == 2 ? 2 : 0);
    p.in.aimX = 200;
    p.in.aimY = p.y - 20;
}

void Game::clearWorld() {
    enemies.clear();
    pending.clear();
    bullets.clear();
    parts.clear();
    popups.clear();
    pickups.clear();
    flaskList.clear();
    trapList.clear();
    fires.clear();
    gases.clear();
    globs.clear();
    hazards.clear();
    arcs.clear();
    pings.clear();
    tlights.clear();
    critters.clear();
    decalClear = true;
}

void Game::startRun(int stageIdx) {
    startStage = std::clamp(stageIdx, 0, STAGE_COUNT - 1);
    wave = startStage * 5;
    stage = startStage;
    loop = 0;
    bossesThisRun = 0;
    dying = lastStand = false;
    deathT = 0;
    newBest = false;
    timeScale = 1;
    paused = false;
    clearWorld();
    toasts.clear();
    if (!online()) {
        players[0].active = true;
        players[0].skin = save.skin;
        players[0].name = net::localName();
        for (int i = 1; i < MAX_SLOTS; i++) players[i].active = false;
        localSlot = 0;
    }
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!players[i].active) continue;
        resetPlayer(players[i], i);
        players[i].coins = STAGE_START_COINS[startStage];
    }
    barrTier = 0;
    barrMax = barrMaxFor(0);
    barrHp = barrMax;
    save.runs++;
    if (activeCount() > 1) save.coopRuns++;
    enterStage(stage, false);
    if (startStage > 0) {
        // Later stages open in the apothecary so the doctors can spend their stipend.
        mode = Mode::Shop;
        shopT = 0;
        banner(STAGES[stage].name, "SPEND YOUR STIPEND WISELY", 0xffd860, 3.f);
    } else {
        startWave();
    }
}

void Game::enterStage(int s, bool announce) {
    stage = s;
    decalClear = true;
    raining = STAGES[s].weather == 0 && chance(0.5f);
    weather.clear();
    if (announce) {
        fxBanner("STAGE " + std::to_string(s + 1) + ": " + STAGES[s].name, STAGES[s].sub, 0xffd860, 3.6f);
        sfx(SFX_BELL, 0.8f, 0.7f);
    }
    syncStageAudio();
}

void Game::syncStageAudio() {
    int s = mode == Mode::Title || mode == Mode::Lobby ? titleStage : stage;
    bool boss = false;
    for (auto& e : enemies) boss |= isBoss(e.t) && !e.dead;
    int track = mode == Mode::Title || mode == Mode::Lobby ? 0 : (boss ? 5 : STAGES[s].music);
    audio::setMusicTrack(track);
    int amb = STAGES[s].ambience;
    if (STAGES[s].weather == 0) amb = raining ? AMB_RAIN : AMB_NONE;
    audio::setAmbience(amb);
    shownStage = s;
}

void Game::buildWave() {
    queue.clear();
    bool boss = wave % 5 == 0;
    int n = std::max(1, activeCount());
    float budget = (6 + wave * 5.f + wave * wave * 0.35f) * (1 + 0.55f * (n - 1));
    float t = 1.5f;
    if (boss) {
        budget *= 0.55f;
        queue.push_back({3.f, STAGES[stage].boss, (LANE_TOP + LANE_BOT) / 2});
        // Later loops bring an old friend along.
        if (loop >= 1) queue.push_back({14.f, STAGES[(stage + 3) % STAGE_COUNT].boss, rnd(LANE_TOP + 6, LANE_BOT - 6)});
    }
    struct Opt { EType t; float cost; int minW; float weight; };
    const Opt opts[] = {{E_RAT, 1, 1, 10},          {E_CROW, 2, 2, 3},        {E_PLAGUERAT, 2, 3, 4},  {E_BLOATER, 4, 4, 2.5f},
                        {E_GIANTRAT, 4, 5, 2.5f},   {E_SPITTER, 4, 6, 2.2f},  {E_GHOUL, 3, 6, 3.f},    {E_BAT, 3, 7, 2.f},
                        {E_BRUTE, 8, 8, 1.6f},      {E_KNIGHT, 7, 11, 2.f},   {E_CULTIST, 6, 12, 1.4f}};
    auto weight = [&](const Opt& o) {
        float w = o.weight;
        for (EType f : STAGES[stage].favored)
            if (f == o.t) w *= 2.5f;
        return w;
    };
    float gap = std::max(0.24f, 1.7f - wave * 0.06f) / std::sqrt(float(n));
    while (budget > 0) {
        if (wave >= 2 && chance(0.12f)) {
            int k = irnd(4, 7);
            for (int i = 0; i < k; i++) queue.push_back({t + rnd(0, 0.9f), E_RAT, rnd(LANE_TOP, LANE_BOT)});
            budget -= k * 0.8f;
            t += 2.2f;
            continue;
        }
        float total = 0;
        for (auto& o : opts)
            if (o.minW <= wave && o.cost <= budget + 1) total += weight(o);
        float pick = rnd(0, total);
        const Opt* chosen = &opts[0];
        for (auto& o : opts) {
            if (o.minW > wave || o.cost > budget + 1) continue;
            if ((pick -= weight(o)) <= 0) { chosen = &o; break; }
        }
        if (chosen->t == E_BAT) {
            // Bats come as a swarm.
            int k = irnd(3, 5);
            for (int i = 0; i < k; i++) queue.push_back({t + rnd(0, 0.6f), E_BAT, 0});
        } else {
            queue.push_back({t, chosen->t, rnd(LANE_TOP, LANE_BOT)});
        }
        budget -= chosen->cost;
        t += gap * rnd(0.5f, 1.4f);
    }
    std::sort(queue.begin(), queue.end(), [](const SpawnEv& a, const SpawnEv& b) { return a.t < b.t; });
}

void Game::startWave() {
    wave++;
    loop = (wave - 1) / (5 * STAGE_COUNT);
    int s = stageForWave(wave);
    if (s != stage || wave == 1) enterStage(s, wave > 1 && s != stage);
    buildWave();
    spawnIdx = 0;
    waveT = 0;
    clearT = -1;
    waveKilled = 0;
    waveTotal = int(queue.size());
    decalClear = true;
    mode = Mode::Play;
    lastStand = false;
    for (int i = 0; i < MAX_SLOTS; i++) {
        Player& p = players[i];
        if (!p.active) continue;
        for (int w = 0; w < WEAPON_COUNT; w++) p.ammo[w] = clipSize(p, w);
        p.reloading = p.jammed = false;
        p.ready = false;
    }
    bool boss = wave % 5 == 0;
    if (STAGES[stage].weather == 0) {
        raining = boss || (wave >= 3 && chance(0.4f));
        syncStageAudio();
    }
    lightningT = rnd(3, 8);
    if (wave > save.bestWave) {
        if (save.bestWave > 0 && !newBest) toast("NEW RECORD!", 0x8aff8a);
        save.bestWave = wave;
        newBest = true;
        checkUnlocks(true);
        persist();
    }
    if (boss) {
        fxBanner(EDEF[STAGES[stage].boss].name, "A HORROR APPROACHES...", 0xff4a4a, 3.4f);
        sfx(SFX_BELL, 0.8f, 0.75f);
    } else if (bannerT <= 0) {
        fxBanner("WAVE " + std::to_string(wave), WAVE_FLAVOR[wave % 6], 0xffd860);
        sfx(SFX_BELL, 0.6f);
    }
}

void Game::endWave() {
    for (auto& p : pickups)
        if (p.owner >= 0 && p.owner < MAX_SLOTS) players[p.owner].coins += p.val;
    pickups.clear();
    int bonus = 25 + wave * 12;
    for (int i = 0; i < MAX_SLOTS; i++) {
        Player& p = players[i];
        if (!p.active) continue;
        p.coins += bonus;
        p.ready = false;
        if (p.down || p.dead) revivePlayer(i, 0.5f, false);
    }
    if (wave % 5 == 0) save.stageClears++;
    persist();
    fxToast("WAVE BONUS +" + std::to_string(bonus), 0xffd860);
    mode = Mode::Shop;
    shopT = 0;
    lmb = false;
    hazards.clear();
    globs.clear();
    gases.clear();
    sfx(SFX_BUY, 0.6f);
}

void Game::gameOver() {
    mode = Mode::GameOver;
    timeScale = 1;
    paused = false;
    save.bestCombo = std::max(save.bestCombo, me().bestCombo);
    persist();
    checkUnlocks(true);
    if (role == Role::Host) net::setStatus("GAME OVER", activeCount());
}

void Game::toTitle(const std::string& msg) {
    mode = Mode::Title;
    paused = false;
    chatOpen = typing = false;
    SDL_StopTextInput(win);
    clearWorld();
    setScreen(msg.empty() ? Screen::Main : Screen::Coop);
    if (!msg.empty()) {
        netMsg = msg;
        netMsgT = 6;
    }
    localSlot = 0;
    for (int i = 1; i < MAX_SLOTS; i++) players[i].active = false;
    players[0].active = true;
    players[0].name = net::localName();
    players[0].skin = save.skin;
    persist();
    syncStageAudio();
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
    bool inGame = mode == Mode::Play && !paused && !chatOpen;
    bool canAct = inGame && me().alive();
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        mapMouse(e.motion.x, e.motion.y);
        keyNav = false;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        mapMouse(e.button.x, e.button.y);
        keyNav = false;
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
                    act(ACT_SWITCH, 0, 0, slot);
                    lmb = false;
                } else {
                    me().in.clicks++;
                }
                clickPending = false;
            }
        } else if (e.button.button == SDL_BUTTON_RIGHT && canAct) {
            act(ACT_FLASK, mx, my, 0);
        } else if (e.button.button == SDL_BUTTON_MIDDLE && inGame) {
            act(ACT_PING, mx, my, 0);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) lmb = false;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (canAct && e.wheel.y != 0) {
            int dir = e.wheel.y > 0 ? -1 : 1;
            const Player& p = me();
            for (int k = 1; k < WEAPON_COUNT; k++) {
                int w = (p.cur + dir * k + WEAPON_COUNT * 2) % WEAPON_COUNT;
                if (p.owned[w]) { act(ACT_SWITCH, 0, 0, w); break; }
            }
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (typing) {
            std::string& dst = chatOpen ? chatText : ipText;
            size_t lim = chatOpen ? 60 : 40;
            for (const char* c = e.text.text; *c; c++)
                if (*c >= 32 && *c < 127 && dst.size() < lim) dst += char(std::toupper((unsigned char)*c));
        }
        break;
    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode k = e.key.key;
        if (typing) {
            std::string& dst = chatOpen ? chatText : ipText;
            if (k == SDLK_BACKSPACE && !dst.empty()) dst.pop_back();
            if (e.key.repeat) break;
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                if (chatOpen) {
                    if (!chatText.empty()) sendChat(chatText);
                    chatText.clear();
                    chatOpen = typing = false;
                    SDL_StopTextInput(win);
                } else {
                    menuKey = 5;
                }
            } else if (k == SDLK_ESCAPE) {
                if (chatOpen) {
                    chatText.clear();
                    chatOpen = typing = false;
                    SDL_StopTextInput(win);
                } else {
                    menuKey = 6;
                }
            }
            break;
        }
        if (e.key.repeat) break;
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
        // Menu-style keyboard navigation for every screen that is not live gameplay.
        bool menuish = mode == Mode::Title || mode == Mode::Lobby || paused || mode == Mode::GameOver;
        if (menuish) {
            keyNav = true;
            if (k == SDLK_UP || k == SDLK_W) menuKey = 1;
            else if (k == SDLK_DOWN || k == SDLK_S) menuKey = 2;
            else if (k == SDLK_LEFT || k == SDLK_A) menuKey = 3;
            else if (k == SDLK_RIGHT || k == SDLK_D) menuKey = 4;
            else if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_KP_ENTER) menuKey = 5;
            else if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) menuKey = 6;
        }
        if (online() && (k == SDLK_RETURN || k == SDLK_T) && !menuish && mode != Mode::Title) {
            chatOpen = typing = true;
            chatText.clear();
            SDL_StartTextInput(win);
            break;
        }
        if (k == SDLK_RETURN && mode == Mode::Lobby && !paused) {
            chatOpen = typing = true;
            chatText.clear();
            SDL_StartTextInput(win);
            menuKey = 0;
            break;
        }
        switch (mode) {
        case Mode::Play:
            if (k == SDLK_ESCAPE || k == SDLK_P) {
                paused = !paused;
                menuSel = 0;
                menuKey = 0;
            } else if (k == SDLK_TAB) showScores = true;
            else if (!canAct) break;
            else if (k == SDLK_R || k == SDLK_SPACE) act(ACT_RELOAD, 0, 0, 0);
            else if (k >= SDLK_1 && k <= SDLK_9) act(ACT_SWITCH, 0, 0, int(k - SDLK_1));
            else if (k == SDLK_0) act(ACT_SWITCH, 0, 0, 9);
            else if (k == SDLK_E) act(ACT_TRAP, mx, my, 0);
            else if (k == SDLK_Q) act(ACT_TONIC, 0, 0, 0);
            else if (k == SDLK_G) act(ACT_FLASK, mx, my, 0);
            else if (k == SDLK_V || (k == SDLK_T && !online())) act(ACT_PING, mx, my, 0);
            break;
        case Mode::Shop:
            if (k == SDLK_ESCAPE) {
                paused = !paused;
                menuSel = 0;
                menuKey = 0;
            } else if (k == SDLK_TAB) showScores = true;
            break;
        default: break;
        }
        break;
    }
    case SDL_EVENT_KEY_UP:
        if (e.key.key == SDLK_TAB) showScores = false;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        lmb = false;
        keyW = keyA = keyS = keyD = keyF = false;
        if (mode == Mode::Play && !dying && !online()) paused = true;
        break;
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET: decalClear = true; break;
    default: break;
    }
}

// Continuous input (movement, aim, held buttons) is read from the keyboard state every frame.
void Game::updateLocalInput() {
    const bool* ks = SDL_GetKeyboardState(nullptr);
    bool live = (mode == Mode::Play || mode == Mode::Shop) && !paused && !chatOpen;
    keyW = live && ks[SDL_SCANCODE_W];
    keyA = live && ks[SDL_SCANCODE_A];
    keyS = live && ks[SDL_SCANCODE_S];
    keyD = live && ks[SDL_SCANCODE_D];
    keyF = live && ks[SDL_SCANCODE_F];
    PlayerInput& in = me().in;
    in.aimX = mx;
    in.aimY = my;
    in.moveX = float(keyD) - float(keyA) + botMoveX;
    in.moveY = float(keyS) - float(keyW) + botMoveY;
    in.lmb = lmb && mode == Mode::Play && !paused && !chatOpen;
    in.interact = (keyF || botInteract) && mode == Mode::Play;
}

// ============================================================ frame update

void Game::update(float dt) {
    time += dt;
    netUpdate(dt);
    updateLocalInput();
    float rdt = dt;
    screenT += rdt;
    netMsgT -= rdt;
    for (auto& c : chats) c.life -= rdt;
    chats.erase(std::remove_if(chats.begin(), chats.end(), [](const Chat& c) { return c.life <= 0; }), chats.end());
    // Offline pause freezes the world; online the world keeps going behind the menu.
    if (paused && !online()) return;

    timeScale = approach(timeScale, dying ? 0.35f : 1.f, rdt * (dying ? 3.f : 1.2f));
    float wdt = hitstop > 0 ? 0 : rdt * timeScale;
    hitstop = std::max(0.f, hitstop - rdt);
    shake = std::max(0.f, shake - rdt * 22);
    bannerT -= rdt;
    coinPulse -= rdt;
    for (auto& p : players) p.hurtFlash -= rdt;
    for (auto& t : toasts) t.life -= rdt;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [](const Toast& t) { return t.life <= 0; }), toasts.end());

    // Track the local doctor's progress for the save file on every machine.
    if (me().kills < countedKills) countedKills = me().kills;
    if (bossesThisRun < countedBosses) countedBosses = bossesThisRun;
    if (me().revives < countedRevives) countedRevives = me().revives;
    if (me().kills > countedKills) {
        save.totalKills += me().kills - countedKills;
        countedKills = me().kills;
        checkUnlocks(true);
    }
    if (bossesThisRun > countedBosses) {
        save.bossKills += bossesThisRun - countedBosses;
        countedBosses = bossesThisRun;
        checkUnlocks(true);
    }
    if (me().revives > countedRevives) {
        save.revives += me().revives - countedRevives;
        countedRevives = me().revives;
    }

    updateAmbient(wdt);
    if (mode == Mode::Play || mode == Mode::Shop) {
        if (authority()) {
            if (mode == Mode::Play) updatePlay(wdt);
            else {
                shopT += rdt;
                for (int i = 0; i < MAX_SLOTS; i++)
                    if (players[i].active) updatePlayer(i, wdt);
                // Everyone ready: the next wave begins.
                bool all = activeCount() > 0;
                for (auto& p : players)
                    if (p.active && !p.ready) all = false;
                if (online() && all) startWave();
            }
        } else {
            updateClient(wdt);
        }
    }
    if (mode == Mode::Title || mode == Mode::Lobby) {
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
    ambientFx(wdt);
    updateParticles(wdt);
}

void Game::updateAmbient(float dt) {
    fogX += dt * 6;
    for (int i = 0; i < 3; i++) {
        cloudX[i] += cloudV[i] * dt;
        if (cloudX[i] > VW + 10) cloudX[i] = -float(A.cloud[i].w) - 10;
    }
    if (mode == Mode::Title || mode == Mode::Lobby) {
        // The title backdrop slowly tours the stages the player has reached.
        titleStageT += dt;
        if (titleStageT > 18) {
            titleStageT = 0;
            int unlocked = 0;
            for (int s = 0; s < STAGE_COUNT; s++)
                if (stageUnlocked(s)) unlocked = s;
            if (unlocked > 0) {
                titleStage = (titleStage + 1) % (unlocked + 1);
                raining = titleStage == 0 && chance(0.5f);
                weather.clear();
                decalClear = true;
                syncStageAudio();
            }
        }
    }
    updateWeather(dt);
    flashT -= dt;
    if (thunderT > 0) {
        thunderT -= dt;
        if (thunderT <= 0) audio::play(SFX_THUNDER, 0.8f, rnd(0.8f, 1.1f));
    }
}

void Game::updateWeather(float dt) {
    int s = mode == Mode::Title || mode == Mode::Lobby ? titleStage : stage;
    int kind = STAGES[s].weather;
    float rate = 0;
    if (kind == 0) rate = raining ? 280 : 0;
    else if (kind == 1) rate = 5;
    else if (kind == 2) rate = 26;
    else rate = 70;
    float n = dt * rate;
    while (n > 0) {
        if (n >= 1 || chance(n)) {
            Drop d{};
            d.ph = rnd(0, 6.28f);
            if (kind == 0) d = {rnd(-70, VW), rnd(-24, -4), rnd(GROUND_Y + 2, VH), 70, 400, 0};
            else if (kind == 1) d = {rnd(0, VW), rnd(GROUND_Y - 30, VH - 10), 9999, rnd(-6, 6), rnd(-4, 2), rnd(0, 6.28f)};
            else if (kind == 2) d = {rnd(-20, VW), VH + 4, -10, rnd(-8, 14), rnd(-40, -18), rnd(0, 6.28f)};
            else d = {rnd(-40, VW), rnd(-10, -2), rnd(GROUND_Y + 2, VH), rnd(4, 14), rnd(18, 34), rnd(0, 6.28f)};
            weather.push_back(d);
        }
        n -= 1;
    }
    for (auto& d : weather) {
        d.ph += dt;
        if (kind == 0) {
            d.x += d.vx * dt;
            d.y += d.vy * dt;
            if (d.y >= d.floorY) {
                for (int k = 0; k < 2; k++) addPart(d.x, d.floorY, rnd(-30, 30), rnd(-50, -20), 0.15f, 0x9ab0d8, 0, 1, 300, d.floorY + 1);
                d.y = 9999;
            }
        } else if (kind == 1) {
            // Marsh motes wander and fade after a while.
            d.x += (d.vx + std::sin(d.ph * 1.3f) * 6) * dt;
            d.y += (d.vy + std::cos(d.ph * 0.9f) * 4) * dt;
            if (d.ph > 9) d.y = 9999;
        } else if (kind == 2) {
            d.x += (d.vx + std::sin(d.ph * 2) * 10) * dt;
            d.y += d.vy * dt;
            if (d.y < d.floorY) d.y = 9999;
        } else {
            d.x += (d.vx + std::sin(d.ph * 1.7f) * 8) * dt;
            d.y += d.vy * dt;
            if (d.y >= d.floorY) d.y = 9999;
        }
    }
    weather.erase(std::remove_if(weather.begin(), weather.end(), [](const Drop& d) { return d.y > 9000; }), weather.end());

    if (raining && kind == 0 && authority()) {
        lightningT -= dt;
        if (lightningT <= 0) {
            lightningT = rnd(7, 16);
            fxThunder(xr());
        }
    }
}

void Game::updatePlay(float dt) {
    waveT += dt;
    while (spawnIdx < queue.size() && queue[spawnIdx].t <= waveT) {
        const SpawnEv& s = queue[spawnIdx++];
        spawnEnemy(s.type, -14 - rnd(0, 10) * (isBoss(s.type) ? 3 : 1), s.y, false);
    }
    for (int i = 0; i < MAX_SLOTS; i++)
        if (players[i].active) updatePlayer(i, dt);
    updateBullets(dt);
    updateEnemies(dt);
    updateMisc(dt);
    for (auto& p : pending) enemies.push_back(p);
    pending.clear();
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.dead || e.x > VW + 40; }),
                  enemies.end());
    static bool bossMusic = false;
    bool boss = false;
    for (auto& e : enemies) boss |= isBoss(e.t);
    if (boss != bossMusic) {
        bossMusic = boss;
        syncStageAudio();
    }

    for (auto& p : players) {
        if (p.comboT > 0) {
            p.comboT -= dt;
            if (p.comboT <= 0) p.combo = 0;
        }
    }

    // Last stand: one doctor left standing in a co-op game fights harder.
    if (activeCount() > 1 && aliveCount() == 1 && !lastStand && !dying) {
        lastStand = true;
        for (int i = 0; i < MAX_SLOTS; i++)
            if (players[i].alive()) fxBanner("LAST STAND!", players[i].name + " FIGHTS ON ALONE", 0xff9a30, 2.6f);
        sfx(SFX_BELL, 0.8f, 1.3f);
    } else if (aliveCount() > 1) {
        lastStand = false;
    }

    if (!dying && aliveCount() == 0) {
        dying = true;
        deathT = 0;
        sfx(SFX_BELL, 0.9f, 0.6f);
        fxBanner(activeCount() > 1 ? "THE DOCTORS HAVE FALLEN" : "THE DOCTOR HAS FALLEN", "", 0xff4040, 3.f);
    }
    if (dying) {
        deathT += dt;
        if (deathT > 1.6f) gameOver();
        return;
    }
    if (spawnIdx >= queue.size() && enemies.empty()) {
        if (clearT < 0) {
            clearT = 0;
            bool stageDone = wave % 5 == 0;
            fxBanner(stageDone ? "STAGE CLEARED" : "WAVE CLEARED", stageDone ? "THE HORROR IS VANQUISHED" : "THE CLINIC STANDS",
                     0x8aff8a, 2.4f);
            sfx(SFX_PERFECT, 0.6f, 0.9f);
        }
        clearT += dt;
        if (clearT > 2.6f) endWave();
    }
}

// ============================================================ doctors

void Game::shoulder(const Player& p, float& sx, float& sy) const {
    int f = int(time * 1.7f) & 1;
    const Sprite& b = A.doctor[p.skin][f];
    float lx = p.faceLeft ? DOC_SHOULDER_X : b.w - DOC_SHOULDER_X;
    sx = p.x - b.w / 2 + lx - std::cos(p.aimAng) * p.recoil;
    sy = p.y - b.h + DOC_SHOULDER_Y + f - std::sin(p.aimAng) * p.recoil;
}

void Game::muzzle(const Player& p, float& x, float& y) const {
    float sx, sy;
    shoulder(p, sx, sy);
    const ArmGfx& a = A.arm[p.skin][p.cur];
    float lx = a.mx - a.px, ly = a.my - a.py;
    if (p.faceLeft) ly = -ly;
    float c = std::cos(p.aimAng), s = std::sin(p.aimAng);
    x = sx + lx * c - ly * s;
    y = sy + lx * s + ly * c;
}

void Game::updatePlayer(int slot, float dt) {
    Player& p = players[slot];
    p.invulnT -= dt;
    if (p.down) {
        if (!p.dead) {
            p.downT += dt;
            p.reviveT = std::max(0.f, p.reviveT - dt * 0.35f);
            if (p.downT >= BLEED_OUT) {
                p.dead = true;
                p.reviveT = 0;
                fxToast(p.name + " HAS BLED OUT", 0xff6050);
            }
        } else {
            p.downT += dt;
        }
        return;
    }
    if (p.dead) return;

    // Movement: the local doctor moves here; remote doctors report their own position.
    bool localCtl = slot == localSlot;
    if (localCtl && (p.in.moveX != 0 || p.in.moveY != 0)) {
        float len = std::hypot(p.in.moveX, p.in.moveY);
        p.x = clampf(p.x + p.in.moveX / len * WALK_SPEED * dt, YARD_X0, YARD_X1);
        p.y = clampf(p.y + p.in.moveY / len * WALK_SPEED * 0.7f * dt, YARD_Y0, YARD_Y1);
        p.walk += dt * 9;
    }
    if (mode != Mode::Play) return;

    float sx, sy;
    shoulder(p, sx, sy);
    float dx = p.in.aimX - sx, dy = p.in.aimY - sy;
    if (dx * dx + dy * dy > 9) p.aimAng = std::atan2(dy, dx);
    p.faceLeft = std::cos(p.aimAng) < 0;
    if (p.in.clicks != p.lastClicks) {
        p.lastClicks = p.in.clicks;
        p.fireQueue = 0.2f;
    }

    const WeaponDef& d = WEAPONS[p.cur];
    p.fireCD -= dt * rateMul(p);
    p.fireQueue -= dt;
    p.flameSfxT -= dt;
    p.recoil = std::max(0.f, p.recoil - dt * 40);
    p.muzzleT -= dt;
    p.crossSpread = std::max(0.f, p.crossSpread - dt * 25);
    if (p.reloading) {
        p.reloadT += dt * rateMul(p);
        if (p.reloadT >= p.reloadDur) {
            p.reloading = p.jammed = false;
            p.ammo[p.cur] = clipSize(p, p.cur);
            fxReload(slot, 2);
        }
    }
    bool want = d.hold ? p.in.lmb : p.fireQueue > 0;
    if (want && !p.reloading && p.fireCD <= 0) {
        if (p.ammo[p.cur] > 0) {
            fire(slot);
            p.fireQueue = 0;
            p.fireCD = d.interval;
            if (p.ammo[p.cur] == 0) startReload(slot);
        } else {
            startReload(slot);
        }
    }
    if (p.in.interact) interact(slot, dt);
    else p.repairSfxT = 0;
}

void Game::interact(int slot, float dt) {
    Player& p = players[slot];
    // Reviving a fallen colleague takes priority over carpentry.
    for (int i = 0; i < MAX_SLOTS; i++) {
        Player& q = players[i];
        if (i == slot || !q.active || !q.down || q.dead) continue;
        if (std::hypot(q.x - p.x, (q.y - p.y) * 1.4f) > 20) continue;
        q.reviveT += dt / REVIVE_TIME + dt * 0.35f;
        if (chance(dt * 20)) fxBurst(q.x, q.y - 6, 1, 0xfff0a0, 6, 20, 0.5f);
        if (q.reviveT >= 1) {
            revivePlayer(i, 0.4f, true);
            p.revives++;
        }
        return;
    }
    if (p.x < BARR_X + 26 && barrHp < barrMax && p.coins > 0) {
        float amt = 32 * dt;
        barrHp = std::min(barrMax, barrHp + amt);
        p.repairAcc += amt * 0.5f;
        while (p.repairAcc >= 1 && p.coins > 0) {
            p.coins--;
            p.repairAcc -= 1;
        }
        p.repairSfxT -= dt;
        if (p.repairSfxT <= 0) {
            p.repairSfxT = 0.32f;
            fxRepair(BARR_X, rnd(LANE_TOP, LANE_BOT));
        }
    }
}

void Game::fire(int slot) {
    Player& p = players[slot];
    const WeaponDef& d = WEAPONS[p.cur];
    float x, y;
    muzzle(p, x, y);
    p.ammo[p.cur]--;
    bool perfect = p.blessedShots > 0;
    if (perfect) p.blessedShots--;
    float dmg = d.dmg * dmgMul(p) * (perfect ? 1.4f : 1.f);
    fxShot(slot, x, y, p.aimAng, p.cur, perfect, xr());
    if (d.kind == K_LIGHTNING) fireLightning(slot, x, y, dmg);
    if (d.kind == K_GAS) {
        float tx = clampf(p.in.aimX, 8, p.x - 24), ty = clampf(p.in.aimY, LANE_TOP, LANE_BOT);
        float dist = std::hypot(tx - x, ty - y);
        fxFlask(x, y, tx, ty, 0.3f + dist / 420.f, 1, slot);
    }
}

void Game::startReload(int slot) {
    Player& p = players[slot];
    if (p.reloading || p.ammo[p.cur] >= clipSize(p, p.cur)) return;
    p.reloading = true;
    p.jammed = false;
    p.reloadT = 0;
    p.reloadDur = WEAPONS[p.cur].reload;
    sfx(SFX_RELOAD, slot == localSlot ? 0.4f : 0.2f, 0.8f);
}

void Game::tryActiveReload(int slot) {
    Player& p = players[slot];
    if (!p.reloading) {
        startReload(slot);
        return;
    }
    if (p.jammed) return;
    float u = p.reloadT / p.reloadDur;
    float lo = p.skin == 5 ? 0.36f : 0.42f, hi = p.skin == 5 ? 0.68f : 0.60f;
    if (u >= lo && u <= hi) {
        p.reloading = false;
        p.ammo[p.cur] = clipSize(p, p.cur);
        p.blessedShots = clipSize(p, p.cur);
        fxReload(slot, 0);
    } else {
        p.jammed = true;
        p.reloadDur += 0.6f;
        fxReload(slot, 1);
    }
}

void Game::throwFlask(int slot, float tx, float ty) {
    Player& p = players[slot];
    if (p.flasks <= 0) {
        fxPopup("NO FLASKS", p.x - 10, p.y - 40, 0xff8060);
        sfx(SFX_EMPTY, 0.5f);
        return;
    }
    p.flasks--;
    float sx, sy;
    shoulder(p, sx, sy);
    tx = clampf(tx, 8, p.x - 24);
    ty = clampf(ty, LANE_TOP, LANE_BOT);
    float dist = std::hypot(tx - sx, ty - sy);
    fxFlask(sx, sy - 4, tx, ty, 0.3f + dist / 480.f, 0, slot);
}

void Game::placeTrap(int slot, float tx, float ty) {
    Player& p = players[slot];
    if (p.traps <= 0) {
        fxPopup("NO TRAPS", p.x - 10, p.y - 40, 0xff8060);
        sfx(SFX_EMPTY, 0.5f);
        return;
    }
    p.traps--;
    tx = clampf(tx, 16, BARR_X - 16);
    ty = clampf(ty, LANE_TOP, LANE_BOT);
    trapList.push_back({tx, ty, 0, false});
    fxTrap(tx, ty, false);
}

void Game::drinkTonic(int slot) {
    Player& p = players[slot];
    if (p.tonics <= 0) {
        fxPopup("NO TONICS", p.x - 10, p.y - 40, 0xff8060);
        sfx(SFX_EMPTY, 0.5f);
        return;
    }
    if (p.hp >= p.maxHp) {
        fxPopup("ALREADY HEALTHY", p.x - 10, p.y - 40, 0xa0ffa0);
        return;
    }
    p.tonics--;
    healPlayer(slot, 45, true);
}

void Game::healPlayer(int slot, float amount, bool big) {
    Player& p = players[slot];
    if (!p.alive()) return;
    float heal = std::min(amount, p.maxHp - p.hp);
    if (heal <= 0) return;
    p.hp += heal;
    fxHealPlayer(slot, heal, big);
}

void Game::switchWeapon(int slot, int w) {
    Player& p = players[slot];
    if (w < 0 || w >= WEAPON_COUNT || !p.owned[w] || w == p.cur) return;
    p.cur = w;
    p.reloading = p.jammed = false;
    p.fireCD = 0.12f;
    if (slot == localSlot) audio::play(SFX_CLICK, 0.5f);
    if (p.ammo[p.cur] == 0) startReload(slot);
}

void Game::doPing(int slot, float x, float y) {
    // Pinging an enemy marks it: everyone deals extra damage to it for a while.
    Enemy* best = nullptr;
    for (auto& e : enemies) {
        if (e.dead) continue;
        SDL_FRect b = enemyBox(e);
        if (x >= b.x - 3 && x <= b.x + b.w + 3 && y >= b.y - 3 && y <= b.y + b.h + 3) best = &e;
    }
    if (best) {
        best->mark = 5;
        fxPopup("MARKED!", best->x, enemyBox(*best).y - 10, SLOT_COL[slot], 1, 1.f);
    }
    fxPing(slot, x, y);
}

void Game::revivePlayer(int slot, float hpFrac, bool announce) {
    Player& p = players[slot];
    p.down = p.dead = false;
    p.downT = p.reviveT = 0;
    p.hp = std::max(1.f, p.maxHp * hpFrac);
    p.invulnT = 2;
    if (announce) {
        fxRevive(slot);
        fxToast(p.name + " IS BACK ON THEIR FEET", 0x8aff8a);
    }
}

void Game::buy(int slot, int cat, int idx) {
    Player& p = players[slot];
    auto deny = [&] { if (slot == localSlot) audio::play(SFX_EMPTY, 0.5f); };
    auto ok = [&](int s) { if (slot == localSlot) audio::play(s, 0.7f); };
    if (cat == 0 && idx >= 0 && idx < WEAPON_COUNT) {
        const WeaponDef& d = WEAPONS[idx];
        if (p.owned[idx]) {
            p.cur = idx;
            if (slot == localSlot) audio::play(SFX_CLICK, 0.5f);
        } else if (p.coins >= d.price) {
            p.coins -= d.price;
            p.owned[idx] = true;
            p.ammo[idx] = clipSize(p, idx);
            p.cur = idx;
            ok(SFX_BUY);
            if (slot == localSlot) toast("ACQUIRED " + std::string(d.name), 0xffd860);
        } else deny();
    } else if (cat == 1 && idx >= 0 && idx < 5) {
        const UpgradeDef& u = UPGRADES[idx];
        int level = idx == 4 ? barrTier : p.up[idx];
        int cost = upgradeCost(p, idx);
        if (level >= u.max || p.coins < cost) return deny();
        p.coins -= cost;
        if (idx == 4) {
            barrTier++;
            barrMax = barrMaxFor(barrTier);
            barrHp = barrMax;
            fxToast(p.name + " FORTIFIED THE BARRICADE", 0xc8a878);
        } else {
            p.up[idx]++;
        }
        if (idx == 2)
            for (int w = 0; w < WEAPON_COUNT; w++) p.ammo[w] = clipSize(p, w);
        if (idx == 3) {
            p.maxHp += 25;
            p.hp = std::min(p.maxHp, p.hp + 25);
        }
        ok(SFX_BUY);
    } else if (cat == 2 && idx >= 0 && idx < 5) {
        const SupplyDef& s = SUPPLIES[idx];
        int cost = s.price;
        bool can = true;
        if (idx == 0) {
            cost = std::max(10, int((barrMax - barrHp) * 0.6f));
            can = barrHp < barrMax;
        } else if (idx == 4) {
            cost = std::max(5, int((p.maxHp - p.hp) * 0.8f));
            can = p.hp < p.maxHp;
        } else {
            int have = idx == 1 ? p.flasks : (idx == 2 ? p.traps : p.tonics);
            can = have < s.max;
        }
        if (!can || p.coins < cost) return deny();
        p.coins -= cost;
        if (idx == 0) barrHp = barrMax;
        else if (idx == 1) p.flasks++;
        else if (idx == 2) p.traps++;
        else if (idx == 3) p.tonics++;
        else p.hp = p.maxHp;
        ok(idx == 4 ? SFX_DRINK : SFX_BUY);
    } else if (cat == 3 && idx >= 0 && idx < MAX_SLOTS && idx != slot && players[idx].active) {
        // Gift coins to a colleague.
        int amt = std::min(p.coins, 50);
        if (amt <= 0) return deny();
        p.coins -= amt;
        players[idx].coins += amt;
        fxToast(p.name + " GAVE " + std::to_string(amt) + " COINS TO " + players[idx].name, 0xffd860);
        sfx(SFX_COIN, 0.5f, 1.2f);
    }
}

void Game::act(int kind, float x, float y, int arg) {
    if (authority()) action(localSlot, kind, x, y, arg);
    else sendAction(kind, x, y, arg);
}

// Every discrete thing a doctor can do. Called directly for the local doctor on the host and for
// ACTION messages arriving from clients.
void Game::action(int slot, int kind, float x, float y, int arg) {
    if (slot < 0 || slot >= MAX_SLOTS || !players[slot].active) return;
    Player& p = players[slot];
    bool live = mode == Mode::Play && p.alive() && !dying;
    switch (kind) {
    case ACT_RELOAD: if (live) tryActiveReload(slot); break;
    case ACT_FLASK: if (live) throwFlask(slot, x, y); break;
    case ACT_TRAP: if (live) placeTrap(slot, x, y); break;
    case ACT_TONIC: if (live) drinkTonic(slot); break;
    case ACT_SWITCH: if (live) switchWeapon(slot, arg); break;
    case ACT_PING: if (mode == Mode::Play) doPing(slot, x, y); break;
    case ACT_BUY: if (mode == Mode::Shop) buy(slot, arg / 100, arg % 100); break;
    case ACT_READY:
        if (mode == Mode::Shop || mode == Mode::Lobby) {
            p.ready = arg != 0;
            if (online()) sfx(SFX_READY, 0.5f, p.ready ? 1.2f : 0.8f);
            if (mode == Mode::Lobby) sendLobby();
        }
        break;
    case ACT_GIFT: if (mode == Mode::Shop) buy(slot, 3, arg); break;
    case ACT_SKIN:
        if (mode == Mode::Lobby && arg >= 0 && arg < SKIN_COUNT) {
            p.skin = arg;
            sendLobby();
        }
        break;
    }
}
