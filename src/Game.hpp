#pragma once
#include "Art.hpp"
#include "Gfx.hpp"
#include "Proto.hpp"
#include "Util.hpp"

#include <string>
#include <vector>

constexpr int MAX_SLOTS = 4;

enum EType {
    E_RAT, E_RATLING, E_PLAGUERAT, E_GIANTRAT, E_CROW, E_BLOATER, E_SPITTER, E_BRUTE,
    E_GHOUL, E_BAT, E_KNIGHT, E_CULTIST,
    E_RATKING, E_ABOM, E_BISHOP, E_HORSEMAN, // bosses last (see isBoss)
    E_COUNT
};
enum class Mode { Title, Lobby, Play, Shop, GameOver };
enum class Screen { Main, Solo, Coop, Browse, JoinIp, Wardrobe, Options, Records };
enum class Role { Offline, Host, Client };

struct Enemy {
    int id = 0;
    EType t = E_RAT;
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
    float hp = 1, maxHp = 1, speed = 0, dmg = 0;
    float atkT = 0, anim = 0, flash = 0, burn = 0, trapT = 0, knock = 0, stT = 0, stT2 = 0, stT3 = 0, lunge = 0;
    float gas = 0, mark = 0, stun = 0;
    float standX = 0, hoverY = 0;
    int state = 0, tgt = 0;
    int lastHit = -1, assist = -1, mask = 0;
    float lastHitT = 0;
    bool dead = false, attacking = false, over = false; // over: past the barricade (ghouls)
    // client-side smoothing targets
    float nx = 0, ny = 0, nz = 0;
    bool seen = false;
};
struct Bullet {
    float x, y, vx, vy, dmg, life, maxLife, blast, knock, floorY, spin;
    int pierce, nh, owner, kind, weapon;
    int hits[8];
    bool perfect, dead, cosmetic;
    uint32_t col;
};
// kind: 0 pixel, 1 smoke, 2 gib (bounces), 3 spark (glow line), 4 fire (glow puff), 5 casing, 6 glow pixel
struct Particle {
    float x, y, vx, vy, life, max, size, grav, floorY, drag;
    uint32_t col;
    uint8_t kind;
    bool rest;
};
struct Popup { std::string s; float x, y, life, max; uint32_t col; int scale; };
struct Pickup { float x, y, vx, vy, t, floorY; int val, owner; };
struct Flask { float x0, y0, x1, y1, t, dur; int kind, owner; bool cosmetic; }; // kind 0 fire flask, 1 gas canister
struct Trap { float x, y, t; bool shut; };
struct FirePatch { float x, y, life; };
struct GasCloud { float x, y, r, life, dps; int owner; };
struct Glob { float x, y, vx, vy; int kind; bool dead; }; // kind 0 acid, 1 scythe, 2 hellfire orb
struct Hazard { float x, y, r, t, dur, dmg; bool done; }; // telegraphed fire pillar
struct Arc { std::vector<std::pair<float, float>> pts; float life; uint32_t col; };
struct Ping { float x, y, life; int slot; };
struct TLight { float x, y, r, life, max; uint32_t col; };
struct Drop { float x, y, floorY, vx, vy, ph; };
struct Critter { float x, y, speed, anim; };
struct SpawnEv { float t; EType type; float y; };
struct Decal { float x, y, r; uint32_t col; };
struct Toast { std::string s; float life; uint32_t col; };
struct Chat { std::string s; float life; uint32_t col; };

struct PlayerInput {
    float aimX = 200, aimY = 120;
    float moveX = 0, moveY = 0;
    bool lmb = false, interact = false;
    uint8_t clicks = 0; // increments on every press so drops can't lose a shot
};

struct Player {
    bool active = false;
    uint64_t peer = 0;
    std::string name;
    int skin = 0;
    bool ready = false;
    float x = 346, y = 190;
    float hp = 100, maxHp = 100;
    bool owned[WEAPON_COUNT] = {};
    int cur = 0;
    int ammo[WEAPON_COUNT] = {};
    bool reloading = false, jammed = false;
    float reloadT = 0, reloadDur = 1;
    int blessedShots = 0;
    float fireCD = 0, recoil = 0, muzzleT = 0, flameSfxT = 0, crossSpread = 0;
    float aimAng = PI;
    bool faceLeft = true;
    int up[4] = {};
    int flasks = 2, traps = 1, tonics = 1;
    int coins = 0;
    int kills = 0, combo = 0, bestCombo = 0, revives = 0, downs = 0;
    float comboT = 0, dmgDealt = 0;
    bool down = false, dead = false;
    float downT = 0, reviveT = 0, invulnT = 0, repairAcc = 0, repairSfxT = 0;
    float fireQueue = 0, walk = 0;
    float nx = 0, ny = 0, nAim = PI; // network targets for smoothing remote doctors
    uint8_t lastClicks = 0;
    PlayerInput in;
    int ping = -1;
    float hurtFlash = 0;
    bool alive() const { return active && !down && !dead; }
};

struct Save {
    int bestWave = 0, totalKills = 0, bossKills = 0, skin = 0;
    int runs = 0, coopRuns = 0, revives = 0, bestCombo = 0, stageClears = 0;
    bool muted = false, shake = true, dmgNumbers = true;
    int volMaster = 8, volMusic = 7, volSfx = 8; // 0..10
    std::string lastIp = "127.0.0.1";
};

class Game {
public:
    bool init(SDL_Window* w, SDL_Renderer* r);
    void shutdown();
    void event(const SDL_Event& e);
    void update(float dt);
    void render();
    // Dev aid: scripted bot run that saves screenshots (ZECLINIC_AUTOTEST=<dir>).
    void autotest(int frame, const std::string& dir);
    bool quit = false;

private:
    // ---- core
    SDL_Window* win = nullptr;
    SDL_Renderer* R = nullptr;
    SDL_Texture *sceneTex = nullptr, *lightTex = nullptr, *decalTex = nullptr, *entityTex = nullptr;
    Mode mode = Mode::Title;
    Screen screen = Screen::Main;
    Role role = Role::Offline;
    Save save;
    std::string savePath;
    float time = 0;
    bool decalClear = true;
    int previewSkin = 0;
    bool paused = false; // pause menu open (only freezes the world offline)

    // ---- menu state
    float screenT = 0;                 // time since the current screen opened (for slide-ins)
    int menuSel = 0, menuHover = -1, lastHover = -1;
    bool keyNav = false;               // last menu input came from the keyboard
    int menuKey = 0;                   // pending keyboard command for the menu: 1 up 2 down 3 left 4 right 5 accept 6 back
    int titleStage = 0;
    float titleStageT = 0;
    int soloStage = 0, lobbyStage = 0;
    std::string ipText, chatText;
    bool typing = false, chatOpen = false;
    std::string netMsg;                // status / error line on the co-op screens
    float netMsgT = 0;
    int browseSel = 0;
    bool showScores = false;
    int menuCount = 0, prevMenuCount = 0, shopTab = 0;
    float selY = 0;                    // animated menu highlight position
    std::string menuHint;

    // ---- input (virtual coordinates)
    float mx = VW / 2, my = VH / 2;
    bool lmb = false, clickPending = false;
    float botMoveX = 0, botMoveY = 0; // autotest steering
    bool botInteract = false;
    bool keyW = false, keyA = false, keyS = false, keyD = false, keyF = false;

    // ---- players
    Player players[MAX_SLOTS];
    int localSlot = 0;
    Player& me() { return players[localSlot]; }
    const Player& me() const { return players[localSlot]; }
    int activeCount() const;
    int aliveCount() const;

    // ---- run state
    int wave = 0, stage = 0, startStage = 0, loop = 0;
    float barrHp = 120, barrMax = 120;
    int barrTier = 0;
    int bossesThisRun = 0;
    int countedKills = 0, countedBosses = 0, countedRevives = 0; // already added to the save file
    bool dying = false, lastStand = false;
    float deathT = 0;
    bool newBest = false;

    // ---- wave state
    std::vector<SpawnEv> queue;
    size_t spawnIdx = 0;
    float waveT = 0, clearT = -1, shopT = 0;
    int waveTotal = 0, waveKilled = 0;
    bool raining = false;
    float lightningT = 5, flashT = 0, thunderT = -1;
    std::vector<std::pair<float, float>> bolt;

    // ---- fx state
    float shake = 0, hitstop = 0, timeScale = 1, coinPulse = 0, coinPitch = 1, coinPitchT = 0;
    float bannerT = 0, bannerMax = 1;
    std::string bannerText, bannerSub;
    uint32_t bannerCol = 0xffffff;
    std::vector<Toast> toasts;
    std::vector<Chat> chats;
    float cloudX[3] = {20, 160, 290}, cloudY[3] = {20, 52, 34}, cloudV[3] = {3, 5, 2};
    float fogX = 0;
    float critterT = 2;
    int shownStage = -1; // stage whose music/ambience is playing

    // ---- entities
    int nextId = 1;
    std::vector<Enemy> enemies, pending;
    std::vector<Bullet> bullets;
    std::vector<Particle> parts;
    std::vector<Popup> popups;
    std::vector<Pickup> pickups;
    std::vector<Flask> flaskList;
    std::vector<Trap> trapList;
    std::vector<FirePatch> fires;
    std::vector<GasCloud> gases;
    std::vector<Glob> globs;
    std::vector<Hazard> hazards;
    std::vector<Arc> arcs;
    std::vector<Ping> pings;
    std::vector<TLight> tlights;
    std::vector<Drop> weather;
    std::vector<Critter> critters;
    std::vector<Decal> decals;

    // ---- persistence
    void load();
    void persist();
    void applyVolumes();
    bool skinUnlocked(int i) const;
    bool stageUnlocked(int s) const;
    void checkUnlocks(bool announce);
    bool unlockedCache[SKIN_COUNT] = {};

    // ---- flow
    void resetPlayer(Player& p, int slot);
    void startRun(int stageIdx);
    void startWave();
    void buildWave();
    void endWave();
    void gameOver();
    void clearWorld();
    void toTitle(const std::string& msg = "");
    void enterStage(int s, bool announce);
    void syncStageAudio();
    int stageForWave(int w) const;

    // ---- stats helpers
    int clipSize(const Player& p, int w) const;
    float dmgMul(const Player& p) const;
    float coinMul(const Player& p) const;
    float critMul(const Player& p) const;
    float rateMul(const Player& p) const;
    float barrMaxFor(int tier) const;
    int upgradeCost(const Player& p, int i) const;
    float hpScale() const;

    // ---- gameplay (authoritative: offline + host)
    void updateAmbient(float dt);
    void updateWeather(float dt);
    void updatePlay(float dt);
    void updatePlayer(int slot, float dt);
    void updateLocalInput();
    void updateBullets(float dt);
    void updateEnemies(float dt);
    void updateFlyer(Enemy& e, float dt);
    void updateBoss(Enemy& e, float dt);
    void updateMisc(float dt);
    void updateParticles(float dt);
    void ambientFx(float dt);
    void shoulder(const Player& p, float& sx, float& sy) const;
    void muzzle(const Player& p, float& x, float& y) const;
    void fire(int slot);
    void fireLightning(int slot, float x, float y, float dmg);
    void startReload(int slot);
    void tryActiveReload(int slot);
    void throwFlask(int slot, float tx, float ty);
    void placeTrap(int slot, float tx, float ty);
    void drinkTonic(int slot);
    void switchWeapon(int slot, int w);
    void doPing(int slot, float x, float y);
    void interact(int slot, float dt);
    void buy(int slot, int cat, int idx);
    void action(int slot, int kind, float x, float y, int arg);
    void act(int kind, float x = 0, float y = 0, int arg = 0); // local doctor: apply or send to host
    Enemy& spawnEnemy(EType t, float x, float y, bool deferred);
    void damageEnemy(Enemy& e, float dmg, bool crit, float knock, int by, bool popup = true);
    void killEnemy(Enemy& e, bool crit, int by);
    void explode(float x, float y, float r, float dmg, bool big, int by);
    void hitBarricade(float d);
    void hurtPlayer(int slot, float d);
    void downPlayer(int slot);
    void revivePlayer(int slot, float hpFrac, bool announce);
    void healPlayer(int slot, float amount, bool fx);
    void spit(float x, float y, float flight, int target, int kind = 0);
    int pickTarget(float x, float y, bool random = false) const;
    SDL_FRect enemyBox(const Enemy& e) const;
    SDL_FRect playerBox(const Player& p) const;
    float enemyStopX(const Enemy& e) const;

    // ---- replicated cosmetic events (host runs them and queues them for clients)
    W evOut;
    template <class F> void emit(uint8_t type, F&& write) {
        if (role != Role::Host) return;
        evOut.u8(type);
        write(evOut);
    }
    void sfx(int id, float vol = 1.f, float pitch = 1.f);
    void fxPopup(const std::string& s, float x, float y, uint32_t col, int scale = 1, float life = 0.8f);
    void fxDamage(float x, float y, float dmg, bool crit);
    void fxBanner(const std::string& t, const std::string& sub, uint32_t col, float dur = 2.6f);
    void fxToast(const std::string& s, uint32_t col);
    void fxShake(float s);
    void fxShot(int slot, float x, float y, float ang, int weapon, bool perfect, uint32_t seed);
    void fxArc(const std::vector<std::pair<float, float>>& pts, uint32_t col);
    void fxExplode(float x, float y, float r, bool big);
    void fxHit(int id, float x, float y, float floorY, uint32_t col, bool crit, float dirX);
    void fxKill(const Enemy& e);
    void fxCoins(float x, float y, int val, int owner);
    void fxFlask(float x0, float y0, float x1, float y1, float dur, int kind, int owner);
    void fxBarricade(bool broken);
    void fxHurt(int slot, float d);
    void fxReload(int slot, int kind); // 0 perfect, 1 jam, 2 done
    void fxHealPlayer(int slot, float amount, bool big);
    void fxTrap(float x, float y, bool snap);
    void fxGlobPop(float x, float y, int kind, bool shot);
    void fxPing(int slot, float x, float y);
    void fxDown(int slot);
    void fxRevive(int slot);
    void fxThunder(uint32_t seed);
    void fxBeam(float x0, float y0, float x1, float y1, uint32_t col);
    void fxBurst(float x, float y, int n, uint32_t col, int kind, float spd, float life);
    void fxPillar(float x, float y, float r);
    void fxRepair(float x, float y);
    void fxClang(float x, float y);
    void fxChat(int slot, const std::string& s);
    void applyEvents(Rd& r);

    // ---- local-only fx helpers
    void addPart(float x, float y, float vx, float vy, float life, uint32_t col, uint8_t kind, float size = 1, float grav = 0,
                 float floorY = 999, float drag = 0);
    void blood(float x, float y, float floorY, uint32_t col, int n, float dirX);
    void gibs(const Enemy& e);
    void popup(const std::string& s, float x, float y, uint32_t col, int scale = 1, float life = 0.8f);
    void banner(const std::string& t, const std::string& sub, uint32_t col, float dur = 2.6f);
    void toast(const std::string& s, uint32_t col);
    void addShake(float s) { if (save.shake) shake = std::min(8.f, shake + s); }
    void spawnCoins(float x, float y, int val, int owner);

    // ---- networking (GameNet.cpp)
    float netSendT = 0, inputSendT = 0, lobbySendT = 0;
    uint32_t snapTick = 0, lastSnapTick = 0;
    float snapAge = 0;
    bool welcomed = false;
    float predCD = 0;     // client-side fire cooldown for predicted shots
    uint8_t runId = 0;
    bool forcePos = false; // accept the host's position for our doctor on the next snapshot    // increments with every run the host starts
    void netUpdate(float dt);
    void netEvents();
    void netReceive();
    void handleHostMsg(uint64_t from, Rd& r);
    void handleClientMsg(Rd& r);
    int slotForPeer(uint64_t peer) const;
    void hostSession(bool steam, bool friendsOnly);
    void joinSession(bool steam, uint64_t lobby, const std::string& addr);
    void leaveSession(const std::string& msg);
    void sendLobby();
    void sendSnapshot();
    void applySnapshot(Rd& r);
    void sendInput();
    void sendAction(int kind, float x, float y, int arg);
    void sendChat(const std::string& s);
    void hostStart();
    void hostToLobby();
    void updateClient(float dt);
    void playerJoined(uint64_t peer, const std::string& name, int skin);
    void playerLeft(int slot);
    bool online() const { return role != Role::Offline; }
    bool authority() const { return role != Role::Client; }

    // ---- rendering
    void drawStageBackground(int s, float alpha);
    void drawBackground();
    void drawBackgroundEmissive();
    void drawEntities();
    void buildLightmap();
    void drawEmissive();
    void drawHud();
    void drawTeam();
    void drawScoreboard();
    void drawChat();
    void drawShop();
    void drawTitle();
    void drawLobby();
    void drawPaused();
    void drawGameOver();
    void drawPlayer(const Player& p, int slot);
    void drawDoctor(float x, float y, float scale, int skin, int weapon, float ang, bool left, bool locked, float recoil, float deathRot);
    void drawEnemy(const Enemy& e, bool flashPass);
    const Sprite& enemySprite(const Enemy& e) const;
    void drawCursor();
    void drawWeapons(float x, float y, float w);
    bool button(float x, float y, float w, float h, const std::string& label, bool enabled = true, uint32_t col = 0xe8d8a8);
    bool menuItem(int idx, float x, float y, float w, const std::string& label, bool enabled = true, const std::string& hint = "");
    bool slider(float x, float y, float w, const std::string& label, int& value, int maxV);
    void panel(float x, float y, float w, float h);
    void bar(float x, float y, float w, float h, float frac, uint32_t col, uint32_t back);
    void computeViewport(float& ox, float& oy, float& scale) const;
    void setScreen(Screen s);
    void menuTitleBar(const std::string& title, const std::string& sub);
    void drawStageCard(int s, float x, float y, float w, float h, bool selected, bool locked);
};
