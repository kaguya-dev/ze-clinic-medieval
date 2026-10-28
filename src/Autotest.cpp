// Dev aid: scripted runs driven by ZECLINIC_AUTOTEST.
//   <dir>        scripted solo session that saves screenshots (BMP) into <dir>
//   play         honest solo bot that plays until it dies and logs per-wave stats
//   host:<dir>   hosts a LAN lobby, starts when a client is ready, bot-plays and saves screenshots
//   join:<dir>   joins 127.0.0.1 (or ZECLINIC_JOIN), readies up, bot-plays and saves screenshots
#include "Defs.hpp"
#include "Net.hpp"
#include "Util.hpp"

#include <cmath>

using namespace util;

void Game::autotest(int frame, const std::string& arg) {
    auto shot = [&](const std::string& dir, const std::string& name) {
        SDL_SetRenderTarget(R, sceneTex);
        if (SDL_Surface* s = SDL_RenderReadPixels(R, nullptr)) {
            SDL_SaveBMP(s, (dir + "/" + name + ".bmp").c_str());
            SDL_DestroySurface(s);
        }
        SDL_SetRenderTarget(R, nullptr);
    };
    // Shared bot: aim at the most urgent enemy's head, shoot, reload well most of the time.
    auto bot = [&](float skill) {
        Player& p = me();
        if (mode != Mode::Play || !p.alive() || dying) {
            lmb = false;
            return;
        }
        const Enemy* best = nullptr;
        float bestScore = -1e9f;
        int nearBarr = 0;
        for (auto& e : enemies) {
            if (e.dead || e.x < -4) continue;
            float score = e.x + (e.t == E_CROW && e.state == 2 ? 400 : 0) + (e.t == E_BLOATER ? 60 : 0) + (e.over ? 300 : 0);
            if (score > bestScore) { bestScore = score; best = &e; }
            if (e.x > BARR_X - 60) nearBarr++;
        }
        if (best) {
            SDL_FRect b = enemyBox(*best);
            mx = b.x + b.w / 2 + rnd(-3, 3);
            my = b.y + b.h * rnd(0.1f, 0.4f);
            lmb = true;
            if (frame % 6 == 0) p.in.clicks++;
            if (nearBarr >= 4 && frame % 90 == 0) act(ACT_FLASK, mx, my, 0);
        } else {
            lmb = false;
        }
        if (p.reloading && !p.jammed && frame % 7 == 0) {
            float u = p.reloadT / p.reloadDur;
            if (u > 0.45f && u < 0.55f && chance(skill)) act(ACT_RELOAD, 0, 0, 0);
        }
        if (p.hp < p.maxHp * 0.4f && p.tonics > 0 && frame % 30 == 0) act(ACT_TONIC, 0, 0, 0);
        // Step out of telegraphed fire pillars; walk over and revive fallen colleagues.
        botMoveX = botMoveY = 0;
        botInteract = false;
        for (int i = 0; i < MAX_SLOTS; i++) {
            const Player& q = players[i];
            if (i == localSlot || !q.active || !q.down || q.dead) continue;
            float dx = q.x - p.x, dy = q.y - p.y;
            if (std::hypot(dx, dy * 1.4f) > 12) {
                botMoveX = std::fabs(dx) > 2 ? (dx > 0 ? 1.f : -1.f) : 0;
                botMoveY = std::fabs(dy) > 2 ? (dy > 0 ? 1.f : -1.f) : 0;
            }
            botInteract = true;
            static bool said = false;
            if (!said) said = true, SDL_Log("[bot] reviving slot %d", i);
        }
        for (auto& h : hazards) {
            if (h.done || std::hypot(p.x - h.x, (p.y - h.y) * 1.5f) > h.r + 6) continue;
            botMoveX = p.x < h.x ? -1.f : 1.f;
            botMoveY = p.y < h.y ? -1.f : 1.f;
            if (p.x <= YARD_X0 + 1 || p.x >= YARD_X1 - 1) botMoveX = -botMoveX;
        }
    };
    // Alternate between the best affordable gun and upgrades, like a sensible player.
    auto shopBot = [&] {
        Player& p = me();
        static bool gunTurn = true;
        gunTurn = !gunTurn;
        const int pref[] = {4, 5, 3, 6, 2, 1};
        if (gunTurn)
            for (int w : pref)
                if (!p.owned[w] && p.coins >= WEAPONS[w].price) { act(ACT_BUY, 0, 0, w); return; }
        for (int i : {0, 1, 4, 2, 3})
            if ((i == 4 ? barrTier : p.up[i]) < UPGRADES[i].max && p.coins >= upgradeCost(p, i)) { act(ACT_BUY, 0, 0, 100 + i); return; }
    };

    if (arg == "play") {
        static int lastWave = 0;
        if (mode == Mode::Title && frame > 2) startRun(SDL_getenv("ZECLINIC_STAGE") ? SDL_atoi(SDL_getenv("ZECLINIC_STAGE")) : 0);
        bot(0.6f);
        if (SDL_getenv("ZECLINIC_TRACE") && mode == Mode::Play && frame % 120 == 0) {
            int n[E_COUNT] = {};
            for (auto& e : enemies) n[e.t]++;
            std::string mix;
            for (int t = 0; t < E_COUNT; t++)
                if (n[t]) mix += std::string(EDEF[t].name) + "x" + std::to_string(n[t]) + " ";
            SDL_Log("  t=%5.1f hp=%3.0f barr=%3.0f wpn=%s ammo=%d | %s", waveT, me().hp, barrHp, WEAPONS[me().cur].name, me().ammo[me().cur], mix.c_str());
        }
        if (mode == Mode::Shop) {
            Player& p = me();
            SDL_Log("wave %2d cleared  t=%5.1fs  coins=%5d  hp=%3.0f/%3.0f  barr=%3.0f/%3.0f  kills=%d", wave, waveT, p.coins, p.hp, p.maxHp,
                    barrHp, barrMax, p.kills);
            lastWave = wave;
            if (barrHp < barrMax * 0.7f) buy(localSlot, 2, 0);
            if (p.hp < p.maxHp * 0.6f) buy(localSlot, 2, 4);
            for (int k = 0; k < 6; k++) shopBot();
            if (p.flasks < 3) buy(localSlot, 2, 1);
            startWave();
        }
        if (mode == Mode::GameOver) {
            SDL_Log("GAME OVER at wave %d (last cleared %d), kills=%d, bestCombo=%d, sim time %.0fs", wave, lastWave, me().kills,
                    me().bestCombo, time);
            quit = true;
        }
        if (time > 2400) { SDL_Log("time cap at wave %d", wave); quit = true; }
        return;
    }

    if (arg.rfind("steam:", 0) == 0) {
        // Steam smoke test: host a lobby, look for it in the browser, leave.
        std::string dir = arg.substr(6);
        if (frame == 5) {
            SDL_Log("[steam] ready=%d name=%s err=%s", net::steamReady(), net::localName().c_str(), net::steamError().c_str());
            if (!net::steamReady()) { quit = true; return; }
            hostSession(true, false);
        }
        if (frame == 300) {
            shot(dir, "steam_lobby");
            net::requestSteamLobbies();
        }
        if (frame == 600) {
            bool found = false;
            for (auto& l : net::lobbies()) {
                SDL_Log("[steam] lobby %llu name=%s status=%s %d/%d", (unsigned long long)l.id, l.name.c_str(), l.status.c_str(), l.members, l.maxMembers);
                found |= l.backend == net::Backend::Steam;
            }
            SDL_Log("[steam] mode=%d role=%d own lobby listed=%d", int(mode), int(role), found);
            leaveSession("");
            quit = true;
        }
        return;
    }
    if (arg.rfind("host:", 0) == 0 || arg.rfind("join:", 0) == 0) {
        bool hosting = arg[0] == 'h';
        std::string dir = arg.substr(5);
        std::string tag = hosting ? "host" : "join";
        static float startAt = -1;
        if (frame == 5) {
            if (hosting) hostSession(false, false);
            else {
                const char* addr = SDL_getenv("ZECLINIC_JOIN");
                joinSession(false, 0, addr ? addr : "127.0.0.1");
            }
        }
        if (frame > 5 && mode == Mode::Title) {
            SDL_Log("[%s] session ended: %s", tag.c_str(), netMsg.c_str());
            quit = true;
            return;
        }
        if (mode == Mode::Lobby) {
            if (!hosting && welcomed && !me().ready && frame % 30 == 0) act(ACT_READY, 0, 0, 1);
            if (hosting && activeCount() >= 2) {
                bool ready = true;
                for (int i = 1; i < MAX_SLOTS; i++)
                    if (players[i].active && !players[i].ready) ready = false;
                if (ready && startAt < 0) startAt = time + 1;
                if (startAt > 0 && time > startAt) {
                    shot(dir, tag + "_00_lobby");
                    lobbyStage = 0;
                    hostStart();
                    SDL_Log("[host] run started with %d doctors", activeCount());
                }
            }
            if (frame == 200) shot(dir, tag + "_00_lobby");
        }
        bot(0.7f);
        static int lastWave = -1;
        if (mode == Mode::Shop) {
            if (lastWave != wave) {
                lastWave = wave;
                SDL_Log("[%s] shop after wave %d: coins=%d hp=%.0f kills=%d peers=%d", tag.c_str(), wave, me().coins, me().hp, me().kills,
                        int(net::peers().size()));
                shot(dir, tag + "_shop_w" + std::to_string(wave));
                for (int k = 0; k < 4; k++) shopBot();
            }
            if (!me().ready && frame % 20 == 0) act(ACT_READY, 0, 0, 1);
        }
        if (mode == Mode::Play && frame % 600 == 0) {
            shot(dir, tag + "_play_" + std::to_string(frame));
            SDL_Log("[%s] frame %d wave %d enemies=%d bullets=%d hp=%.0f barr=%.0f", tag.c_str(), frame, wave, int(enemies.size()),
                    int(bullets.size()), me().hp, barrHp);
            for (int i = 0; i < MAX_SLOTS; i++)
                if (players[i].active) SDL_Log("[%s]   slot %d%s at %.0f,%.0f", tag.c_str(), i, i == localSlot ? " (me)" : "", players[i].x, players[i].y);
        }
        static bool downedOnce = false;
        if (!downedOnce && wave == 1 && waveT > 8 && mode == Mode::Play && hosting) {
            downedOnce = true;
            // Exercise co-op systems: a ping, and a downed colleague the bot can revive.
            act(ACT_PING, 150, 180, 0);
            players[1].invulnT = 0;
            hurtPlayer(1, 9999);
            SDL_Log("[host] downed slot 1: down=%d", players[1].down);
        }
        static bool wasDown = false;
        if (mode == Mode::Play && wasDown && !players[1].down) SDL_Log("[%s] slot 1 revived (revives by me: %d)", tag.c_str(), me().revives);
        wasDown = players[1].active && players[1].down;
        if (mode == Mode::GameOver || (wave >= 3 && mode == Mode::Shop && frame > 400)) {
            shot(dir, tag + "_end");
            SDL_Log("[%s] finished at wave %d, mode %d, kills=%d", tag.c_str(), wave, int(mode), me().kills);
            quit = true;
        }
        if (frame > 60 * 240) {
            SDL_Log("[%s] time cap", tag.c_str());
            quit = true;
        }
        return;
    }

    // Scripted screenshot tour.
    const std::string& dir = arg;
    if (mode == Mode::Play && !dying) {
        bot(0.8f);
        if (frame % 240 == 120) act(ACT_FLASK, mx, my, 0);
    }
    auto stageJump = [&](int s) {
        wave = s * 5 + 3;
        stage = -1;
        for (auto& p : players) p.coins = 20000;
        for (bool& o : me().owned) o = true;
        startWave();
        queue.clear();
        spawnIdx = 0;
        waveTotal = 30;
    };
    switch (frame) {
    case 40: shot(dir, "01_title"); break;
    case 41: setScreen(Screen::Solo); break;
    case 70: shot(dir, "02_solo"); break;
    case 71: setScreen(Screen::Coop); break;
    case 100: shot(dir, "03_coop"); break;
    case 101: setScreen(Screen::Options); break;
    case 130: shot(dir, "04_options"); break;
    case 131: setScreen(Screen::Main); startRun(0); break;
    case 420: shot(dir, "05_wave1"); break;
    case 700:
        for (auto& e : enemies) e.dead = true;
        spawnIdx = queue.size();
        break;
    case 900: shot(dir, "06_shop"); break;
    case 901: shopTab = 1; break;
    case 920: shot(dir, "07_shop2"); break;
    case 921:
        stageJump(1);
        spawnEnemy(E_ABOM, 60, 196, false);
        spawnEnemy(E_GHOUL, 200, 186, false);
        spawnEnemy(E_GHOUL, 240, 176, false);
        for (int k = 0; k < 4; k++) spawnEnemy(E_BAT, 100 + k * 20, 0, false);
        spawnEnemy(E_BLOATER, 160, 192, false);
        me().cur = 7;
        break;
    case 1150: shot(dir, "08_marsh"); break;
    case 1151:
        clearWorld();
        stageJump(2);
        spawnEnemy(E_BISHOP, -20, 100, false);
        spawnEnemy(E_KNIGHT, 230, 184, false);
        spawnEnemy(E_KNIGHT, 200, 174, false);
        spawnEnemy(E_CULTIST, 120, 190, false);
        me().cur = 6;
        break;
    case 1400: shot(dir, "09_cathedral"); break;
    case 1401: me().cur = 8; break;
    case 1500: shot(dir, "10_gas"); break;
    case 1501:
        clearWorld();
        stageJump(3);
        spawnEnemy(E_HORSEMAN, -30, 186, false);
        spawnEnemy(E_BRUTE, 200, 190, false);
        me().cur = 5;
        break;
    case 1750: shot(dir, "11_abbey"); break;
    case 1751: me().cur = 9; barrHp = 1; hitBarricade(5); break;
    case 1850: shot(dir, "12_broken"); break;
    case 1851: hurtPlayer(localSlot, 9999); break;
    case 2000: shot(dir, "13_gameover"); break;
    case 2010: quit = true; break;
    }
}
