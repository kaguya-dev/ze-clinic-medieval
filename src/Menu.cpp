// Menus: title screens, stage select, co-op browser, lobby, shop, pause and game over.
#include "Defs.hpp"
#include "Net.hpp"
#include "Util.hpp"

#include <algorithm>
#include <cmath>

using namespace util;

namespace {
std::string num(int v) { return std::to_string(v); }
bool inside(float x, float y, float rx, float ry, float rw, float rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
void glow(float x, float y, float r, uint32_t col, float a) {
    gfx::tex(A.light, x - r, y - r, r * 2, r * 2, argb(col, uint32_t(clampf(a, 0, 1) * 255)), SDL_BLENDMODE_ADD);
}
// Ease-out slide used by every screen as it opens.
float slideIn(float t, float delay = 0) { return 1 - smooth((t - delay) * 4); }
} // namespace

// ============================================================ widgets

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
    if (hov) gfx::fillRect(x + 1, y + 1, w - 2, 1, argb(0xffd860, 90));
    font::draw(label, x + w / 2, y + std::floor((h - 5) / 2), argb(enabled ? (hov ? 0xffffff : col) : 0x5a5058), 1, 1);
    if (hov && clickPending) {
        clickPending = false;
        audio::play(SFX_CLICK, 0.5f);
        return true;
    }
    return false;
}

// A vertical menu entry with mouse + keyboard selection and an animated gold highlight.
bool Game::menuItem(int idx, float x, float y, float w, const std::string& label, bool enabled, const std::string& hint) {
    menuCount = std::max(menuCount, idx + 1);
    const float h = 13;
    bool hov = !keyNav && inside(mx, my, x - 4, y - 1, w + 8, h);
    if (hov && menuSel != idx) {
        menuSel = idx;
        audio::play(SFX_HOVER, 0.35f, 1.f + idx * 0.03f);
    }
    bool sel = menuSel == idx;
    float slide = slideIn(screenT, idx * 0.04f) * 60;
    x -= slide;
    if (sel) {
        selY = lerp(selY, y, 0.35f);
        float wob = std::sin(time * 6) * 1.5f;
        gfx::fillRect(x - 4, selY - 1, w + 8, h, argb(0x3a2410, 210));
        gfx::fillRect(x - 4, selY - 1, 2, h, argb(enabled ? 0xffd860 : 0x6a5a4a));
        gfx::fillRect(x - 2, selY + h - 2, w + 6, 1, argb(0xffd860, 80));
        gfx::draw(A.chevron, x - 12 + wob, y + 3, false, 1, false, argb(enabled ? 0xffd860 : 0x6a5a4a));
        if (!hint.empty()) menuHint = hint;
    }
    uint32_t col = !enabled ? 0x5a5058 : (sel ? 0xffffff : 0xc8b8a0);
    font::draw(label, x + (sel ? 5 : 2), y + 3, argb(col), 1, 0, sel);
    bool act = enabled && ((hov && clickPending) || (sel && menuKey == 5));
    if (act) {
        clickPending = false;
        menuKey = 0;
        audio::play(SFX_CLICK, 0.6f);
    } else if (!enabled && sel && menuKey == 5) {
        menuKey = 0;
        audio::play(SFX_EMPTY, 0.4f);
    }
    return act;
}

bool Game::slider(float x, float y, float w, const std::string& label, int& value, int maxV) {
    int before = value;
    font::draw(label, x, y, argb(0xc8b8a0), 1, 0, false);
    float bx = x + 70, bw = w - 90;
    for (int k = 0; k < maxV; k++) {
        float sx = bx + k * (bw / maxV);
        gfx::fillRect(sx, y - 1, bw / maxV - 2, 7, argb(k < value ? 0xd8a838 : 0x2a2230));
    }
    font::draw(num(value * 10) + "%", x + w, y, argb(0xffffff), 1, 2, false);
    if (lmb && inside(mx, my, bx - 4, y - 3, bw + 8, 11)) value = std::clamp(int(std::round((mx - bx) / bw * maxV)), 0, maxV);
    return value != before;
}

void Game::setScreen(Screen s) {
    screen = s;
    screenT = 0;
    menuSel = 0;
    keyNav = false;
    if (s == Screen::JoinIp) {
        typing = true;
        SDL_StartTextInput(win);
    } else if (typing && !chatOpen) {
        typing = false;
        SDL_StopTextInput(win);
    }
    if (s == Screen::Browse) {
        net::requestSteamLobbies();
        net::requestLanHosts();
        browseSel = 0;
    }
}

void Game::menuTitleBar(const std::string& title, const std::string& sub) {
    float off = slideIn(screenT) * 20;
    gfx::fillRect(0, 8 - off, VW, 26, argb(0x05030a, 170));
    gfx::fillRect(0, 34 - off, VW, 1, argb(0x8a6a3a));
    font::draw(title, 16, 12 - off, argb(0xffd860), 2, 0, true);
    font::draw(sub, 16, 26 - off, argb(0xa89880), 1, 0, false);
}

void Game::drawStageCard(int s, float x, float y, float w, float h, bool selected, bool locked) {
    const StageArt& st = A.stage[s];
    uint32_t tint = locked ? argb(0x404048) : 0xFFFFFFFF;
    gfx::tex(st.sky, x, y, w, h, tint);
    gfx::tex(st.town, x, y, w, h, tint);
    float gy = y + h * GROUND_Y / VH;
    gfx::tex(st.ground, x, gy, w, y + h - gy, tint);
    gfx::rect(x - 1, y - 1, w + 2, h + 2, argb(selected ? 0xffd860 : 0x5a4a3a));
    if (selected) {
        gfx::rect(x - 2, y - 2, w + 4, h + 4, argb(0xffd860, uint32_t(120 + 100 * std::sin(time * 5))));
        glow(x + w / 2, y + h / 2, w * 0.7f, 0xffb050, 0.12f);
    }
    if (locked) gfx::draw(A.lock, x + w / 2 - 3, y + h / 2 - 3);
    font::draw(num(s + 1), x + 3, y + 3, argb(0xffffff), 1, 0, true);
}

// ============================================================ title

void Game::drawTitle() {
    if (screen != Screen::Main) {
        if (menuKey == 6) {
            setScreen(Screen::Main);
            audio::play(SFX_CLICK, 0.5f, 0.8f);
            menuKey = 0;
        }
    }
    prevMenuCount = std::max(1, menuCount);
    menuCount = 0;
    menuHint.clear();
    if (menuKey == 1) menuSel = (menuSel + prevMenuCount - 1) % prevMenuCount, audio::play(SFX_HOVER, 0.35f);
    if (menuKey == 2) menuSel = (menuSel + 1) % prevMenuCount, audio::play(SFX_HOVER, 0.35f);

    // Left shade band so the menu reads over any stage.
    for (int x = 0; x < 190; x += 2) gfx::fillRect(x, 0, 2, VH, argb(0x05030a, uint32_t(200 * (1 - x / 190.f) * (1 - x / 190.f) + 20)));
    gfx::fillRect(0, VH - 12, VW, 12, argb(0x000000, 160));
    std::string steam = net::steamReady() ? "STEAM: " + net::localName() : "STEAM: " + net::steamError();
    font::draw(steam, VW - 4, VH - 9, argb(net::steamReady() ? 0x6a9a6a : 0x8a6a6a), 1, 2, false);
    font::draw("V2.0 CO-OP", 4, VH - 9, argb(0x6a5a4a), 1, 0, false);

    auto wavy = [&](const std::string& s, float x0, float y, int scale, float phase) {
        for (size_t i = 0; i < s.size(); i++) {
            float yo = std::sin(time * 2.5f + i * 0.45f + phase) * (scale >= 4 ? 2.f : 1.f);
            uint32_t c = mixRGB(0xffd860, 0xd84a2a, 0.5f + 0.5f * std::sin(time * 1.5f + i * 0.3f + phase));
            font::draw(std::string(1, s[i]), x0 + i * 4 * scale, y + yo, argb(c), scale, 0, true);
        }
    };

    if (screen == Screen::Main) {
        float lo = slideIn(screenT) * 40;
        wavy("ZE CLINIC", 14 - lo, 12, 4, 0);
        font::draw("IF HE BORNED IN A MEDIAVEL ERA", 16 - lo, 36, argb(0xe8a860), 1, 0, true);
        font::draw("A PLAGUE DOCTOR'S LAST STAND", 16 - lo, 45, argb(0xa8c890), 1, 0, false);
        font::draw("- NOW WITH UP TO 4 DOCTORS -", 16 - lo, 54, argb(0x6a8aaa, uint32_t(160 + 80 * std::sin(time * 2))), 1, 0, false);
        float y = 74;
        if (menuItem(0, 22, y, 120, "PLAY SOLO", true, "DEFEND THE CLINIC ALONE")) setScreen(Screen::Solo);
        if (menuItem(1, 22, y += 15, 120, "PLAY CO-OP", true, "STEAM LOBBIES OR LAN - UP TO 4 DOCTORS")) setScreen(Screen::Coop);
        if (menuItem(2, 22, y += 15, 120, "WARDROBE", true, "MASKS AND ROBES WITH SMALL PERKS")) setScreen(Screen::Wardrobe);
        if (menuItem(3, 22, y += 15, 120, "OPTIONS", true, "VOLUME, SCREEN SHAKE, DISPLAY")) setScreen(Screen::Options);
        if (menuItem(4, 22, y += 15, 120, "RECORDS", true, "YOUR DEEDS SO FAR")) setScreen(Screen::Records);
        if (menuItem(5, 22, y += 15, 120, "QUIT", true, "THE PLAGUE WILL WAIT")) quit = true;
        // The doctor poses on the right with a rotating arsenal.
        float cx = 290, fy = 172;
        int wpn = int(time / 3) % WEAPON_COUNT;
        gfx::tex(A.shadow, cx - 30, fy - 5, 60, 10, argb(0xffffff, 200));
        float sxs = cx - 27 + DOC_SHOULDER_X * 3, sys = fy - 28 * 3 + DOC_SHOULDER_Y * 3;
        float ang = std::atan2(my - sys, mx - sxs);
        bool left = std::cos(ang) < 0;
        drawDoctor(cx, fy, 3, save.skin, wpn, ang, left, false, 0, 0);
        int f = int(time * 1.7f) & 1;
        float ex = cx - 27 + (left ? 7 : 9) * 3, ey = fy - 84 + (8 + f) * 3;
        gfx::fillRect(ex, ey, 6, 6, argb(SKINS[save.skin].eye));
        glow(ex + 3, ey + 3, 10, SKINS[save.skin].eye, 0.5f);
        font::draw(SKINS[save.skin].name, cx, fy + 6, argb(0xffd860), 1, 1, true);
        font::draw(WEAPONS[wpn].name, cx, fy + 15, argb(0xa89880), 1, 1, false);
        font::draw("STAGE: " + std::string(STAGES[titleStage].name), cx, 66, argb(0x8a7a6a), 1, 1, false);
    } else if (screen == Screen::Solo) {
        menuTitleBar("SOLO DEFENCE", "CHOOSE WHERE THE NIGHT BEGINS. LATER STAGES START WITH A STIPEND.");
        if (menuKey == 3) soloStage = (soloStage + STAGE_COUNT - 1) % STAGE_COUNT, audio::play(SFX_HOVER, 0.4f);
        if (menuKey == 4) soloStage = (soloStage + 1) % STAGE_COUNT, audio::play(SFX_HOVER, 0.4f);
        float cw = 84, ch = 50, gap = 8, x0 = VW / 2 - (cw * STAGE_COUNT + gap * (STAGE_COUNT - 1)) / 2;
        for (int s = 0; s < STAGE_COUNT; s++) {
            float x = x0 + s * (cw + gap), y = 50 + slideIn(screenT, s * 0.05f) * 60;
            bool locked = !stageUnlocked(s);
            if (inside(mx, my, x, y, cw, ch) && clickPending && !keyNav) {
                clickPending = false;
                soloStage = s;
                audio::play(SFX_CLICK, 0.5f);
            }
            drawStageCard(s, x, y, cw, ch, s == soloStage, locked);
            font::draw(STAGES[s].name, x + cw / 2, y + ch + 4, argb(locked ? 0x6a6060 : (s == soloStage ? 0xffd860 : 0xc8b8a0)), 1, 1, false);
            font::draw("BOSS: " + std::string(EDEF[STAGES[s].boss].name).substr(4), x + cw / 2, y + ch + 12, argb(locked ? 0x5a5050 : 0xa07070), 1, 1, false);
        }
        bool locked = !stageUnlocked(soloStage);
        std::string info = locked ? "LOCKED: REACH WAVE " + num(soloStage * 5 + 1) + " TO UNLOCK"
                                  : (soloStage == 0 ? "WAVE 1 - THE CLASSIC NIGHT" : "WAVE " + num(soloStage * 5 + 1) + " - STIPEND " + num(STAGE_START_COINS[soloStage]) + " COINS");
        font::draw(info, VW / 2, 128, argb(locked ? 0xff7060 : 0xa8e0a0), 1, 1, false);
        font::draw("< LEFT / RIGHT >", VW / 2, 137, argb(0x6a5a4a), 1, 1, false);
        if (menuItem(0, 150, 150, 84, "BEGIN THE PURGE", !locked, "")) startRun(soloStage);
        if (menuItem(1, 150, 166, 84, "BACK")) setScreen(Screen::Main);
    } else if (screen == Screen::Coop) {
        menuTitleBar("CO-OP", "HOLD THE LINE TOGETHER. SHARED BARRICADE, OWN PURSES, REVIVES.");
        bool st = net::steamReady();
        float y = 48;
        if (menuItem(0, 30, y, 150, "HOST STEAM LOBBY", st, "FRIENDS-ONLY LOBBY. INVITE THEM FROM THE LOBBY OR STEAM OVERLAY")) hostSession(true, true);
        if (menuItem(1, 30, y += 15, 150, "HOST PUBLIC STEAM LOBBY", st, "ANYONE BROWSING CAN JOIN")) hostSession(true, false);
        if (menuItem(2, 30, y += 15, 150, "BROWSE GAMES", true, "STEAM LOBBIES AND GAMES ON YOUR LOCAL NETWORK")) setScreen(Screen::Browse);
        if (menuItem(3, 30, y += 15, 150, "HOST LAN GAME", true, "UDP PORT " + num(net::DEFAULT_PORT) + ". FRIENDS ON YOUR NETWORK CAN JOIN")) hostSession(false, false);
        if (menuItem(4, 30, y += 15, 150, "JOIN BY ADDRESS", true, "TYPE AN IP ADDRESS (LAN OR PORT-FORWARDED)")) setScreen(Screen::JoinIp);
        if (menuItem(5, 30, y += 15, 150, "BACK")) setScreen(Screen::Main);
        panel(212, 46, 160, 106);
        font::draw("HOW CO-OP WORKS", 220, 53, argb(0xffd860), 1, 0, false);
        const char* lines[] = {"- THE HOST RUNS THE NIGHT", "- EACH DOCTOR HAS A PURSE", "- STAKES ARE SHARED", "- HOLD F TO REVIVE / REPAIR",
                               "- MMB / V: MARK A FOE (+15%)", "- CROSSFIRE: +20% & ASSISTS", "- LAST ONE UP: LAST STAND", "- ENTER / T: CHAT", "- TAB: SCOREBOARD"};
        for (int i = 0; i < 9; i++) font::draw(lines[i], 220, 64 + i * 9, argb(0xc8b8a0), 1, 0, false);
        font::draw(st ? "STEAM READY (SPACEWAR APP 480)" : "STEAM UNAVAILABLE: " + net::steamError(), VW / 2, 160, argb(st ? 0x8aff8a : 0xff8060), 1, 1, false);
    } else if (screen == Screen::Browse) {
        menuTitleBar("FIND A CLINIC", "STEAM LOBBIES AND LAN GAMES");
        const auto& L = net::lobbies();
        int n = std::min<int>(int(L.size()), 7);
        if (n == 0) font::draw(int(screenT * 2) % 2 ? "SEARCHING..." : "SEARCHING", VW / 2, 70, argb(0xa89880), 1, 1, false);
        for (int i = 0; i < n; i++) {
            const net::LobbyInfo& li = L[i];
            std::string tag = li.backend == net::Backend::Steam ? "[STEAM] " : "[LAN] ";
            std::string label = tag + li.name + "  " + num(li.members) + "/" + num(li.maxMembers) + "  " + li.status;
            bool full = li.maxMembers > 0 && li.members >= li.maxMembers;
            if (menuItem(i, 30, 44 + i * 14, 320, label, !full, li.backend == net::Backend::Lan ? li.address : "STEAM LOBBY")) {
                if (li.backend == net::Backend::Steam) joinSession(true, li.id, "");
                else joinSession(false, 0, li.address);
                return;
            }
        }
        if (menuItem(n, 30, 150, 100, "REFRESH")) {
            net::requestSteamLobbies();
            net::requestLanHosts();
            audio::play(SFX_PING, 0.4f);
        }
        if (menuItem(n + 1, 30, 165, 100, "BACK")) setScreen(Screen::Coop);
    } else if (screen == Screen::JoinIp) {
        menuTitleBar("JOIN BY ADDRESS", "HOST:PORT (DEFAULT PORT " + num(net::DEFAULT_PORT) + ")");
        panel(60, 60, 264, 30);
        font::draw(ipText + ((int(time * 3) & 1) ? "_" : ""), 70, 72, argb(0xffffff), 2, 0, false);
        if (menuItem(0, 150, 110, 84, "CONNECT", !ipText.empty())) joinSession(false, 0, ipText);
        if (menuItem(1, 150, 125, 84, "BACK")) setScreen(Screen::Coop);
    } else if (screen == Screen::Wardrobe) {
        menuTitleBar("WARDROBE", "EVERY MASK CARRIES A SMALL BLESSING");
        if (menuKey == 3) previewSkin = (previewSkin + SKIN_COUNT - 1) % SKIN_COUNT, audio::play(SFX_HOVER, 0.4f);
        if (menuKey == 4) previewSkin = (previewSkin + 1) % SKIN_COUNT, audio::play(SFX_HOVER, 0.4f);
        float cx = VW / 2, fy = 150;
        bool locked = !skinUnlocked(previewSkin);
        // Neighbouring skins as dim silhouettes.
        for (int k : {-1, 1}) {
            int s = (previewSkin + k + SKIN_COUNT) % SKIN_COUNT;
            drawDoctor(cx + k * 90, fy - 6, 2, s, 0, k < 0 ? 0 : PI, k > 0, true, 0, 0);
        }
        gfx::tex(A.shadow, cx - 30, fy - 5, 60, 10, argb(0xffffff, 200));
        float sxs = cx - 27 + DOC_SHOULDER_X * 3, sys = fy - 28 * 3 + DOC_SHOULDER_Y * 3;
        float ang = std::atan2(my - sys, mx - sxs);
        drawDoctor(cx, fy, 3, previewSkin, int(time / 2) % WEAPON_COUNT, ang, std::cos(ang) < 0, locked, 0, 0);
        const Skin& sk = SKINS[previewSkin];
        font::draw(sk.name, cx, fy + 6, argb(locked ? 0x8a8080 : 0xffd860), 1, 1, true);
        font::draw(sk.perk, cx, fy + 15, argb(locked ? 0x6a6060 : 0xa8e0a0), 1, 1);
        if (locked) font::draw("LOCKED: " + sk.unlock, cx, fy + 24, argb(0xff7060), 1, 1);
        else if (previewSkin == save.skin) font::draw("EQUIPPED", cx, fy + 24, argb(0x8aff8a), 1, 1);
        if (button(cx - 70, 92, 14, 16, "<")) previewSkin = (previewSkin + SKIN_COUNT - 1) % SKIN_COUNT;
        if (button(cx + 56, 92, 14, 16, ">")) previewSkin = (previewSkin + 1) % SKIN_COUNT;
        font::draw(num(previewSkin + 1) + "/" + num(SKIN_COUNT), cx, 40, argb(0x9a9080), 1, 1);
        if (!locked && previewSkin != save.skin) {
            save.skin = previewSkin;
            players[0].skin = save.skin;
            persist();
        }
        if (menuItem(0, 20, 190, 60, "BACK")) setScreen(Screen::Main);
    } else if (screen == Screen::Options) {
        menuTitleBar("OPTIONS", "LEFT/RIGHT ADJUSTS THE SELECTED SETTING");
        float x = 90, y = 50, w = 200;
        auto row = [&](int idx, const std::string& label, int* vol, bool* toggle) {
            bool sel = menuSel == idx;
            if (!keyNav && inside(mx, my, x - 6, y - 3, w + 12, 12) && menuSel != idx) {
                menuSel = idx;
                audio::play(SFX_HOVER, 0.35f);
            }
            menuCount = std::max(menuCount, idx + 1);
            if (sel) {
                gfx::fillRect(x - 6, y - 3, w + 12, 12, argb(0x3a2410, 200));
                gfx::fillRect(x - 6, y - 3, 2, 12, argb(0xffd860));
            }
            bool changed = false;
            if (vol) {
                changed = slider(x, y, w, label, *vol, 10);
                if (sel && menuKey == 3 && *vol > 0) (*vol)--, changed = true;
                if (sel && menuKey == 4 && *vol < 10) (*vol)++, changed = true;
            } else {
                font::draw(label, x, y, argb(sel ? 0xffffff : 0xc8b8a0), 1, 0, false);
                font::draw(*toggle ? "ON" : "OFF", x + w, y, argb(*toggle ? 0x8aff8a : 0xff7060), 1, 2, false);
                bool hit = (sel && (menuKey == 3 || menuKey == 4 || menuKey == 5)) || (!keyNav && clickPending && inside(mx, my, x - 6, y - 3, w + 12, 12));
                if (hit) {
                    *toggle = !*toggle;
                    clickPending = false;
                    changed = true;
                }
            }
            if (changed) {
                applyVolumes();
                audio::setMuted(save.muted);
                audio::play(SFX_CLICK, 0.5f);
                persist();
            }
            y += 16;
        };
        row(0, "MASTER", &save.volMaster, nullptr);
        row(1, "MUSIC", &save.volMusic, nullptr);
        row(2, "EFFECTS", &save.volSfx, nullptr);
        row(3, "MUTE ALL (M)", nullptr, &save.muted);
        row(4, "SCREEN SHAKE", nullptr, &save.shake);
        row(5, "DAMAGE NUMBERS", nullptr, &save.dmgNumbers);
        bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
        bool fs2 = fs;
        row(6, "FULLSCREEN (F11)", nullptr, &fs2);
        if (fs2 != fs) SDL_SetWindowFullscreen(win, fs2);
        if (menuItem(7, x, y + 6, 60, "BACK")) setScreen(paused ? Screen::Main : Screen::Main);
    } else if (screen == Screen::Records) {
        menuTitleBar("RECORDS", "WHAT THE TOWNSFOLK WHISPER ABOUT YOU");
        float y = 50;
        auto line = [&](const std::string& k, const std::string& v) {
            font::draw(k, VW / 2 - 6, y, argb(0xa89890), 1, 2, false);
            font::draw(v, VW / 2 + 6, y, argb(0xffffff), 1, 0, false);
            y += 11;
        };
        line("BEST WAVE", num(save.bestWave));
        line("VERMIN SLAIN", num(save.totalKills));
        line("BOSSES SLAIN", num(save.bossKills));
        line("STAGES CLEARED", num(save.stageClears));
        line("BEST COMBO", "X" + num(save.bestCombo));
        line("NIGHTS DEFENDED", num(save.runs));
        line("CO-OP NIGHTS", num(save.coopRuns));
        line("COLLEAGUES REVIVED", num(save.revives));
        int skins = 0, stages = 0;
        for (int i = 0; i < SKIN_COUNT; i++) skins += skinUnlocked(i);
        for (int i = 0; i < STAGE_COUNT; i++) stages += stageUnlocked(i);
        line("SKINS UNLOCKED", num(skins) + "/" + num(SKIN_COUNT));
        line("STAGES UNLOCKED", num(stages) + "/" + num(STAGE_COUNT));
        if (menuItem(0, VW / 2 - 30, 172, 60, "BACK")) setScreen(Screen::Main);
    }

    if (!menuHint.empty()) font::draw(menuHint, VW / 2, VH - 22, argb(0xd8c8a0, uint32_t(200 + 55 * std::sin(time * 3))), 1, 1, false);
    if (netMsgT > 0 && !netMsg.empty()) {
        float a = clampf(netMsgT, 0, 1);
        gfx::fillRect(0, 186, VW, 11, argb(0x2a0a10, uint32_t(200 * a)));
        font::draw(netMsg, VW / 2, 189, argb(0xff9a80, uint32_t(255 * a)), 1, 1, false);
    }
    for (auto& t : toasts) {
        font::draw(t.s, VW / 2, 178, argb(t.col, uint32_t(255 * clampf(t.life, 0, 1))), 1, 1, true);
        break;
    }
}

// ============================================================ lobby

void Game::drawLobby() {
    gfx::fillRect(0, 0, VW, VH, argb(0x05030a, 120));
    bool host = role == Role::Host;
    std::string kind = net::backend() == net::Backend::Steam ? "STEAM" : "LAN";
    menuTitleBar("CO-OP LOBBY", kind + (host ? " - YOU ARE THE HOST" : " - HOSTED BY " + players[0].name));
    if (menuKey == 6) {
        leaveSession("");
        return;
    }
    if (!host && !welcomed) {
        font::draw(netMsg.empty() ? "CONNECTING..." : netMsg, VW / 2, 90, argb(0xffd860), 1, 1, true);
        for (int k = 0; k < 3; k++) gfx::fillRect(VW / 2 - 8 + k * 6, 102, 3, 3, argb(0xffd860, (int(time * 4) % 3) == k ? 255 : 80));
        if (button(VW / 2 - 40, 130, 80, 15, "CANCEL")) leaveSession("");
        return;
    }

    // Player cards
    const float cw = 88, ch = 104, gap = 6, x0 = VW / 2 - (cw * MAX_SLOTS + gap * (MAX_SLOTS - 1)) / 2;
    for (int i = 0; i < MAX_SLOTS; i++) {
        Player& p = players[i];
        float x = x0 + i * (cw + gap), y = 42 + slideIn(screenT, i * 0.06f) * 80;
        gfx::fillRect(x, y, cw, ch, argb(0x140f1a, 220));
        gfx::rect(x, y, cw, ch, argb(p.active ? (p.ready ? 0x5a8a4a : 0x8a6a3a) : 0x2e2830));
        gfx::fillRect(x + 1, y + 1, cw - 2, 2, argb(SLOT_COL[i], p.active ? 255 : 60));
        if (!p.active) {
            font::draw("OPEN", x + cw / 2, y + 40, argb(0x5a5058), 1, 1, false);
            font::draw("WAITING FOR", x + cw / 2, y + 54, argb(0x4a4048), 1, 1, false);
            font::draw("A DOCTOR...", x + cw / 2, y + 62, argb(0x4a4048), 1, 1, false);
            continue;
        }
        float cx = x + cw / 2, fy = y + 72;
        gfx::tex(A.shadow, cx - 20, fy - 4, 40, 8, argb(0xffffff, 200));
        float bobAng = PI + std::sin(time * 1.3f + i) * 0.3f;
        drawDoctor(cx, fy, 2, p.skin, (int(time / 2) + i) % WEAPON_COUNT, bobAng, true, false, 0, 0);
        font::draw(p.name, cx, y + 8, argb(i == localSlot ? 0xffffff : SLOT_COL[i]), 1, 1, true);
        if (i == 0) gfx::draw(A.crown, cx - 3, y + 16);
        font::draw(SKINS[p.skin].name, cx, fy + 4, argb(0xa89880), 1, 1, false);
        bool rd = p.ready || (i == 0 && host);
        font::draw(i == 0 ? "HOST" : (p.ready ? "READY" : "NOT READY"), cx, fy + 14, argb(rd ? 0x8aff8a : 0xc8a050), 1, 1, false);
        if (i != localSlot && p.ping >= 0) font::draw(num(p.ping) + "MS", x + cw - 3, y + 8, argb(0x6a8a6a), 1, 2, false);
        if (i == localSlot) {
            auto cycle = [&](int dir) {
                int s = p.skin;
                for (int k = 0; k < SKIN_COUNT; k++) {
                    s = (s + dir + SKIN_COUNT) % SKIN_COUNT;
                    if (skinUnlocked(s)) break;
                }
                save.skin = s;
                persist();
                act(ACT_SKIN, 0, 0, s);
                p.skin = s;
            };
            if (button(x + 4, y + 40, 12, 14, "<")) cycle(-1);
            if (button(x + cw - 16, y + 40, 12, 14, ">")) cycle(1);
            if (menuKey == 3) cycle(-1);
            if (menuKey == 4) cycle(1);
        }
    }

    // Stage choice
    float sy = 152;
    font::draw("STARTING STAGE", 18, sy, argb(0xc8a878), 1, 0, false);
    drawStageCard(lobbyStage, 18, sy + 9, 56, 32, true, false);
    font::draw(STAGES[lobbyStage].name, 80, sy + 12, argb(0xffd860), 1, 0, false);
    font::draw("WAVE " + num(lobbyStage * 5 + 1) + (lobbyStage ? "  STIPEND " + num(STAGE_START_COINS[lobbyStage]) : ""), 80, sy + 21, argb(0xa89880), 1, 0, false);
    if (host) {
        auto pick = [&](int dir) {
            int s = lobbyStage;
            for (int k = 0; k < STAGE_COUNT; k++) {
                s = (s + dir + STAGE_COUNT) % STAGE_COUNT;
                if (stageUnlocked(s)) break;
            }
            lobbyStage = s;
            sendLobby();
        };
        if (button(80, sy + 29, 12, 12, "<")) pick(-1);
        if (button(96, sy + 29, 12, 12, ">")) pick(1);
    } else {
        font::draw("THE HOST PICKS THE STAGE", 80, sy + 31, argb(0x6a5a4a), 1, 0, false);
    }

    // Actions
    float bx = 220, by = 152;
    bool allReady = true;
    int others = 0;
    for (int i = 1; i < MAX_SLOTS; i++)
        if (players[i].active) {
            others++;
            allReady &= players[i].ready;
        }
    if (host) {
        std::string lbl = others == 0 ? "START (SOLO HOST)" : (allReady ? "START THE NIGHT" : "WAITING FOR READY");
        if (button(bx, by, 152, 15, lbl, allReady, 0xffd860)) hostStart();
    } else {
        Player& m = me();
        if (button(bx, by, 152, 15, m.ready ? "READY! (CLICK TO UNREADY)" : "I'M READY", true, m.ready ? 0x8aff8a : 0xffd860)) {
            act(ACT_READY, 0, 0, m.ready ? 0 : 1);
            audio::play(SFX_READY, 0.5f);
        }
    }
    bool steam = net::backend() == net::Backend::Steam;
    if (button(bx, by + 18, 74, 14, "INVITE", steam)) net::inviteFriends();
    if (button(bx + 78, by + 18, 74, 14, host ? "CLOSE LOBBY" : "LEAVE")) {
        leaveSession("");
        return;
    }
    font::draw(steam ? "INVITE OPENS THE STEAM OVERLAY" : "LAN PORT " + num(net::DEFAULT_PORT), bx + 76, by + 36, argb(0x6a5a4a), 1, 1, false);
    font::draw("ENTER: CHAT", 18, VH - 10, argb(0x6a5a4a), 1, 0, false);
    for (auto& t : toasts) {
        font::draw(t.s, VW / 2, 36, argb(t.col, uint32_t(255 * clampf(t.life, 0, 1))), 1, 1, true);
        break;
    }
}

// ============================================================ shop

void Game::drawShop() {
    Player& p = me();
    gfx::fillRect(0, 0, VW, VH, argb(0x07050c, 150));
    panel(6, 4, VW - 12, VH - 8);
    font::draw("THE APOTHECARY", 14, 10, argb(0xffd860), 2, 0, true);
    gfx::draw(A.coin[0], VW - 64, 11);
    font::draw(num(p.coins), VW - 14, 12, argb(0xffd860), 1, 2);
    font::draw("WAVE " + num(wave) + " SURVIVED - NEXT: " + std::string(STAGES[stageForWave(wave + 1)].name), VW - 14, 21, argb(0x8aff8a), 1, 2, false);

    const char* tabs[2] = {"ARSENAL", "UPGRADES & SUPPLIES"};
    float tx = 14;
    for (int t = 0; t < 2; t++) {
        float w = font::width(tabs[t]) + 12;
        bool hov = inside(mx, my, tx, 28, w, 11);
        gfx::fillRect(tx, 28, w, 11, argb(shopTab == t ? 0x3a2a1a : (hov ? 0x2a2030 : 0x1a1420), 240));
        gfx::rect(tx, 28, w, 11, argb(shopTab == t ? 0xffd860 : 0x5a4a3a));
        font::draw(tabs[t], tx + 6, 31, argb(shopTab == t ? 0xffffff : 0xa89880), 1, 0, false);
        if (hov && clickPending) {
            clickPending = false;
            shopTab = t;
            audio::play(SFX_CLICK, 0.5f);
        }
        tx += w + 4;
    }
    gfx::fillRect(12, 39, VW - 24, 1, argb(0x8a6a3a));

    std::string desc;
    auto row = [&](float x, float y, float w, float h, bool selected) {
        bool hov = inside(mx, my, x, y, w, h);
        gfx::fillRect(x, y, w, h, argb(selected ? 0x23331f : (hov ? 0x33263a : 0x1c1624), 235));
        gfx::rect(x, y, w, h, argb(hov ? 0xffd860 : (selected ? 0x5a8a4a : 0x3a2e36)));
        bool clicked = hov && clickPending;
        if (clicked) clickPending = false;
        return std::pair<bool, bool>{hov, clicked};
    };
    auto price = [&](float x, float y, int cost) {
        gfx::draw(A.coin[0], x, y - 1);
        font::draw(num(cost), x + 9, y, argb(p.coins >= cost ? 0xffd860 : 0xc05050), 1, 0, false);
    };
    const float rowH = 22;
    if (shopTab == 0) {
        for (int i = 0; i < WEAPON_COUNT; i++) {
            const WeaponDef& d = WEAPONS[i];
            float x = 14 + (i / 5) * 180, y = 44 + (i % 5) * rowH, w = 174;
            auto [hov, clicked] = row(x, y, w, rowH - 2, p.owned[i] && p.cur == i);
            const Sprite& ic = A.weaponIcon[i];
            gfx::draw(ic, x + 3, y + std::floor((rowH - 2 - ic.h) / 2));
            float lx = x + 3 + std::max(24, ic.w + 4);
            font::draw(d.name, lx, y + 4, argb(0xf0e8d8), 1, 0, false);
            if (p.owned[i]) font::draw(p.cur == i ? "EQUIPPED" : "OWNED - EQUIP", lx, y + 12, argb(p.cur == i ? 0x8aff8a : 0x9a9080), 1, 0, false);
            else price(lx, y + 12, d.price);
            font::draw(i == 9 ? "0" : num(i + 1), x + w - 4, y + 4, argb(0x5a4a3a), 1, 2, false);
            if (hov) {
                char buf[96];
                if (d.kind == K_FLAME) std::snprintf(buf, sizeof buf, "  %.1f/TICK  FUEL %d", d.dmg * dmgMul(p), clipSize(p, i));
                else if (d.kind == K_GAS) std::snprintf(buf, sizeof buf, "  %d DMG/S  CLIP %d", int(d.dmg * dmgMul(p)), clipSize(p, i));
                else std::snprintf(buf, sizeof buf, "  DMG %dX%d  CLIP %d  RELOAD %.1fS", int(d.dmg * dmgMul(p)), d.pellets, clipSize(p, i), d.reload / rateMul(p));
                desc = std::string(d.desc) + buf;
            }
            if (clicked) act(ACT_BUY, 0, 0, i);
        }
    } else {
        for (int i = 0; i < 5; i++) {
            const UpgradeDef& u = UPGRADES[i];
            float x = 14, y = 44 + i * rowH, w = 174;
            auto [hov, clicked] = row(x, y, w, rowH - 2, false);
            font::draw(u.name, x + 4, y + 4, argb(0xf0e8d8), 1, 0, false);
            int level = i == 4 ? barrTier : p.up[i];
            for (int k = 0; k < u.max; k++) {
                gfx::fillRect(x + 64 + k * 6, y + 4, 4, 5, argb(0x0d0a12));
                gfx::fillRect(x + 64 + k * 6, y + 4, 4, 4, argb(k < level ? 0x8aff8a : 0x3a3440));
            }
            if (i == 4) font::draw("TEAM", x + w - 4, y + 4, argb(0x8ad8ff), 1, 2, false);
            bool maxed = level >= u.max;
            if (maxed) font::draw("MASTERED", x + 4, y + 12, argb(0x8aff8a), 1, 0, false);
            else price(x + 4, y + 12, upgradeCost(p, i));
            if (hov) desc = u.desc;
            if (clicked) act(ACT_BUY, 0, 0, 100 + i);
        }
        for (int i = 0; i < 5; i++) {
            const SupplyDef& s = SUPPLIES[i];
            float x = 194, y = 44 + i * rowH, w = 174;
            auto [hov, clicked] = row(x, y, w, rowH - 2, false);
            font::draw(s.name, x + 4, y + 4, argb(0xf0e8d8), 1, 0, false);
            int cost = s.price;
            std::string status;
            if (i == 0) {
                cost = std::max(10, int((barrMax - barrHp) * 0.6f));
                if (barrHp >= barrMax) status = "INTACT";
                bar(x + 120, y + 13, 46, 3, barrHp / barrMax, 0xc88a3a, 0x2a1a10);
            } else if (i == 4) {
                cost = std::max(5, int((p.maxHp - p.hp) * 0.8f));
                if (p.hp >= p.maxHp) status = "HEALTHY";
                bar(x + 120, y + 13, 46, 3, p.hp / p.maxHp, 0xd02a3a, 0x3a1018);
            } else {
                int have = i == 1 ? p.flasks : (i == 2 ? p.traps : p.tonics);
                font::draw(num(have) + "/" + num(s.max), x + w - 4, y + 4, argb(0xa89880), 1, 2, false);
                if (have >= s.max) status = "FULL";
            }
            if (!status.empty()) font::draw(status, x + 4, y + 12, argb(0x8aff8a), 1, 0, false);
            else price(x + 4, y + 12, cost);
            if (hov) desc = s.desc;
            if (clicked) act(ACT_BUY, 0, 0, 200 + i);
        }
    }

    gfx::fillRect(14, 155, VW - 28, 1, argb(0x3a2a1e));
    if (desc.empty()) desc = online() ? "HOVER AN ITEM FOR DETAILS. EVERY DOCTOR SHOPS WITH THEIR OWN PURSE." : "HOVER AN ITEM FOR DETAILS. COINS ARE KEPT BETWEEN WAVES.";
    font::draw(desc, 16, 159, argb(0xd8d0c0), 1, 0, false);

    if (online()) {
        // Colleagues: ready state and coin gifts.
        float x = 16, y = 170;
        for (int i = 0; i < MAX_SLOTS; i++) {
            const Player& q = players[i];
            if (!q.active) continue;
            gfx::fillRect(x, y, 2, 7, argb(SLOT_COL[i]));
            font::draw(q.name, x + 4, y + 1, argb(i == localSlot ? 0xffffff : SLOT_COL[i]), 1, 0, false);
            font::draw(q.ready ? "READY" : "SHOPPING", x + 4, y + 9, argb(q.ready ? 0x8aff8a : 0xa89880), 1, 0, false);
            if (i != localSlot && button(x + 4, y + 17, 46, 10, "GIFT 50", p.coins > 0, 0xffd860)) act(ACT_GIFT, 0, 0, i);
            x += 56;
        }
        bool all = true;
        for (auto& q : players)
            if (q.active && !q.ready && &q != &p) all = false;
        if (button(VW - 134, 176, 120, 16, p.ready ? "READY - WAITING" : "READY UP >", true, p.ready ? 0x8aff8a : 0xffd860))
            act(ACT_READY, 0, 0, p.ready ? 0 : 1);
        if (role == Role::Host && !all && button(VW - 134, 194, 120, 10, "FORCE START", true, 0xc08060)) startWave();
    } else {
        int pct = int(clampf(p.hp / p.maxHp, 0, 1) * 100);
        font::draw("HEALTH " + num(pct) + "%   BEST WAVE " + num(std::max(save.bestWave, wave)), 16, 182, argb(0x9a9080), 1, 0, false);
        if (button(VW - 150, 177, 134, 16, "DEFEND THE CLINIC >", true, 0xffd860)) startWave();
    }
    for (auto& t : toasts) {
        font::draw(t.s, 120, 170, argb(t.col, uint32_t(255 * clampf(t.life, 0, 1))), 1, 1, true);
        break;
    }
}

// ============================================================ pause / game over

void Game::drawPaused() {
    prevMenuCount = std::max(1, menuCount);
    menuCount = 0;
    menuHint.clear();
    if (menuKey == 1) menuSel = (menuSel + prevMenuCount - 1) % prevMenuCount;
    if (menuKey == 2) menuSel = (menuSel + 1) % prevMenuCount;
    if (menuKey == 6) {
        paused = false;
        menuKey = 0;
        return;
    }
    gfx::fillRect(0, 0, VW, VH, argb(0x05030a, online() ? 130 : 170));
    font::draw(online() ? "MENU" : "PAUSED", VW / 2, 40, argb(0xffd860), 3, 1, true);
    if (online()) font::draw("THE NIGHT GOES ON WITHOUT YOU...", VW / 2, 62, argb(0xff8060), 1, 1, false);
    float x = VW / 2 - 50, y = 80;
    int i = 0;
    if (menuItem(i++, x, y, 100, "RESUME")) paused = false;
    if (menuItem(i++, x, y += 15, 100, audio::muted() ? "SOUND: OFF" : "SOUND: ON")) {
        save.muted = !save.muted;
        audio::setMuted(save.muted);
        persist();
    }
    if (menuItem(i++, x, y += 15, 100, save.shake ? "SCREEN SHAKE: ON" : "SCREEN SHAKE: OFF")) {
        save.shake = !save.shake;
        persist();
    }
    if (role == Role::Host && menuItem(i++, x, y += 15, 100, "RETURN TO LOBBY")) {
        hostToLobby();
        return;
    }
    std::string leave = role == Role::Host ? "END SESSION" : (role == Role::Client ? "LEAVE GAME" : "ABANDON RUN");
    if (menuItem(i++, x, y += 15, 100, leave)) {
        if (online()) leaveSession("");
        else toTitle();
    }
}

void Game::drawGameOver() {
    prevMenuCount = std::max(1, menuCount);
    menuCount = 0;
    menuHint.clear();
    if (menuKey == 1 || menuKey == 3) menuSel = (menuSel + prevMenuCount - 1) % prevMenuCount;
    if (menuKey == 2 || menuKey == 4) menuSel = (menuSel + 1) % prevMenuCount;
    gfx::fillRect(0, 0, VW, VH, argb(0x1a0408, 170));
    font::draw("THE PLAGUE PREVAILS", VW / 2, 22, argb(0xff4a4a), 3, 1, true);
    font::draw(activeCount() > 1 ? "THE CLINIC HAS FALLEN. THE DOCTORS FOUGHT BRAVELY." : "THE CLINIC HAS FALLEN. THE RATS FEAST TONIGHT.",
               VW / 2, 44, argb(0xc8a8a0), 1, 1);
    font::draw("REACHED WAVE " + num(wave) + " IN " + std::string(STAGES[stage].name) + (loop ? " (LOOP " + num(loop + 1) + ")" : ""), VW / 2, 56,
               argb(0xffd860), 1, 1, false);

    // Team table
    float w = 300, x = VW / 2 - w / 2, y = 70;
    const char* heads[] = {"DOCTOR", "KILLS", "DAMAGE", "COMBO", "REVIVES", "DOWNS"};
    const float cx[] = {x + 8, x + 120, x + 170, x + 214, x + 254, x + 292};
    gfx::fillRect(x, y - 3, w, 12 + activeCount() * 11, argb(0x0d0a12, 160));
    for (int k = 0; k < 6; k++) font::draw(heads[k], cx[k], y, argb(0xc8a878), 1, k == 0 ? 0 : 2, false);
    float ry = y + 11;
    int mvp = -1;
    float best = -1;
    for (int i = 0; i < MAX_SLOTS; i++)
        if (players[i].active && players[i].dmgDealt > best) best = players[i].dmgDealt, mvp = i;
    for (int i = 0; i < MAX_SLOTS; i++) {
        const Player& p = players[i];
        if (!p.active) continue;
        font::draw(p.name, cx[0], ry, argb(i == localSlot ? 0xffffff : SLOT_COL[i]), 1, 0, false);
        if (i == mvp && activeCount() > 1) gfx::draw(A.crown, cx[0] + font::width(p.name) + 3, ry - 1);
        std::string vals[] = {num(p.kills), num(int(p.dmgDealt)), "X" + num(p.bestCombo), num(p.revives), num(p.downs)};
        for (int k = 0; k < 5; k++) font::draw(vals[k], cx[k + 1], ry, argb(0xe8e0cc), 1, 2, false);
        ry += 11;
    }
    font::draw("BOSSES SLAIN " + num(bossesThisRun) + "   BEST WAVE EVER " + num(save.bestWave), VW / 2, ry + 6, argb(0xa89890), 1, 1, false);
    if (newBest && int(time * 3) % 2 == 0) font::draw("NEW RECORD!", VW / 2, ry + 16, argb(0x8aff8a), 1, 1, true);

    float by = 172;
    if (role == Role::Offline) {
        if (menuItem(0, VW / 2 - 110, by, 90, "TRY AGAIN")) startRun(startStage);
        if (menuItem(1, VW / 2 + 20, by, 90, "RETURN TO TITLE")) toTitle();
    } else if (role == Role::Host) {
        if (menuItem(0, VW / 2 - 110, by, 90, "BACK TO LOBBY")) hostToLobby();
        if (menuItem(1, VW / 2 + 20, by, 90, "END SESSION")) leaveSession("");
    } else {
        font::draw("WAITING FOR THE HOST...", VW / 2, by - 10, argb(0xa89880), 1, 1, false);
        if (menuItem(0, VW / 2 - 45, by, 90, "LEAVE GAME")) leaveSession("");
    }
    for (auto& t : toasts) {
        font::draw(t.s, VW / 2, 196, argb(t.col, uint32_t(255 * clampf(t.life, 0, 1))), 1, 1, true);
        break;
    }
}
