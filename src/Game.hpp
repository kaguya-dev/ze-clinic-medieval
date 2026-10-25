#pragma once
#include "Art.hpp"
#include "Gfx.hpp"
#include "Util.hpp"

#include <string>
#include <vector>

enum EType { E_RAT, E_RATLING, E_PLAGUERAT, E_GIANTRAT, E_CROW, E_BLOATER, E_SPITTER, E_BRUTE, E_RATKING, E_ABOM, E_COUNT };
enum class Mode { Title, Play, Shop, Paused, GameOver };

struct Enemy {
    int id = 0;
    EType t = E_RAT;
    float x = 0, y = 0, vx = 0, vy = 0;
    float hp = 1, maxHp = 1, speed = 0, dmg = 0;
    float atkT = 0, anim = 0, flash = 0, burn = 0, trapT = 0, knock = 0, stT = 0, stT2 = 0, lunge = 0;
    float standX = 0, hoverY = 0;
    int state = 0;
    bool dead = false, attacking = false;
};
struct Bullet {
    float x, y, vx, vy, dmg, life, maxLife, blast, knock;
    int pierce, nh;
    int hits[8];
    bool flame, perfect, dead;
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
struct Pickup { float x, y, vx, vy, t, floorY; int val; };
struct Flask { float x0, y0, x1, y1, t, dur; };
struct Trap { float x, y, t; bool shut; };
struct FirePatch { float x, y, life; };
struct Glob { float x, y, vx, vy; bool dead; };
struct Light { float x, y, r; uint32_t col; float a; };
struct TLight { float x, y, r, life, max; uint32_t col; };
struct Drop { float x, y, floorY; };
struct Critter { float x, y, speed, anim; };
struct SpawnEv { float t; EType type; float y; };
struct Decal { float x, y, r; uint32_t col; };
struct Toast { std::string s; float life; uint32_t col; };

struct Save {
    int bestWave = 0, totalKills = 0, bossKills = 0, skin = 0;
    bool muted = false;
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
    Save save;
    std::string savePath;
    float time = 0;
    bool decalClear = true;
    int previewSkin = 0;

    // ---- input (virtual coordinates)
    float mx = VW / 2, my = VH / 2;
    bool lmb = false, clickPending = false;
    float fireQueue = 0;

    // ---- run state
    int wave = 0, coins = 0;
    float hp = 100, maxHp = 100;
    float barrHp = 120, barrMax = 120;
    int barrTier = 0;
    bool owned[WEAPON_COUNT] = {true, false, false, false, false};
    int cur = 0;
    int ammo[WEAPON_COUNT] = {};
    bool reloading = false, jammed = false;
    float reloadT = 0, reloadDur = 1;
    int blessedShots = 0;
    float fireCD = 0, recoil = 0, muzzleT = 0, flameSfxT = 0, crossSpread = 0;
    float aimAng = PI;
    bool faceLeft = true;
    int up[5] = {};
    int flasks = 2, traps = 1, tonics = 1;
    int runKills = 0, combo = 0, bestCombo = 0;
    float comboT = 0;
    int bossesThisRun = 0;
    bool dying = false;
    float deathT = 0;
    bool newBest = false;

    // ---- wave state
    std::vector<SpawnEv> queue;
    size_t spawnIdx = 0;
    float waveT = 0, clearT = -1;
    int waveTotal = 0, waveKilled = 0;
    bool raining = false;
    float lightningT = 5, flashT = 0, thunderT = -1;
    std::vector<std::pair<float, float>> bolt;

    // ---- fx state
    float shake = 0, hitstop = 0, timeScale = 1, hurtFlash = 0, coinPulse = 0, coinPitch = 1, coinPitchT = 0;
    float bannerT = 0, bannerMax = 1;
    std::string bannerText, bannerSub;
    uint32_t bannerCol = 0xffffff;
    std::vector<Toast> toasts;
    float cloudX[3] = {20, 160, 290}, cloudY[3] = {20, 52, 34}, cloudV[3] = {3, 5, 2};
    float fogX = 0;
    float critterT = 2;

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
    std::vector<Glob> globs;
    std::vector<TLight> tlights;
    std::vector<Light> lights;
    std::vector<Drop> rain;
    std::vector<Critter> critters;
    std::vector<Decal> decals;

    // ---- persistence
    void load();
    void persist();
    bool skinUnlocked(int i) const;
    void checkUnlocks(bool announce);
    bool unlockedCache[SKIN_COUNT] = {};

    // ---- flow
    void startRun();
    void startWave();
    void buildWave();
    void endWave();
    void gameOver();

    // ---- stats helpers
    int clipSize(int w) const;
    float dmgMul() const;
    float coinMul() const;
    float critMul() const;
    float rateMul() const;
    float barrMaxFor(int tier) const;
    int upgradeCost(int i) const;

    // ---- gameplay
    void updateAmbient(float dt);
    void updatePlay(float dt);
    void updateDoctor(float dt);
    void updateBullets(float dt);
    void updateEnemies(float dt);
    void updateCrow(Enemy& e, float dt);
    void updateMisc(float dt);
    void updateParticles(float dt);
    void shoulder(float& sx, float& sy) const;
    void muzzle(float& x, float& y) const;
    void fire();
    void startReload();
    void tryActiveReload();
    void throwFlask();
    void placeTrap();
    void drinkTonic();
    void switchWeapon(int w);
    Enemy& spawnEnemy(EType t, float x, float y, bool deferred);
    void damageEnemy(Enemy& e, float dmg, bool crit, float knock, bool popup = true);
    void killEnemy(Enemy& e, bool crit);
    void explode(float x, float y, float r, float dmg, bool big);
    void hitBarricade(float d);
    void hurtDoctor(float d);
    void spit(float x, float y, float flight);
    SDL_FRect enemyBox(const Enemy& e) const;

    // ---- fx helpers
    void addPart(float x, float y, float vx, float vy, float life, uint32_t col, uint8_t kind, float size = 1, float grav = 0,
                 float floorY = 999, float drag = 0);
    void blood(float x, float y, float floorY, uint32_t col, int n, float dirX);
    void gibs(const Enemy& e);
    void popup(const std::string& s, float x, float y, uint32_t col, int scale = 1, float life = 0.8f);
    void banner(const std::string& t, const std::string& sub, uint32_t col, float dur = 2.6f);
    void toast(const std::string& s, uint32_t col);
    void addShake(float s) { shake = std::min(8.f, shake + s); }
    void spawnCoins(float x, float y, int val);

    // ---- rendering
    void drawBackground();
    void drawBackgroundEmissive();
    void drawEntities();
    void buildLightmap();
    void drawEmissive();
    void drawHud();
    void drawShop();
    void drawTitle();
    void drawPaused();
    void drawGameOver();
    void drawDoctor(float x, float y, float scale, int skin, float ang, bool left, bool locked);
    void drawEnemy(const Enemy& e, bool flashPass);
    const Sprite& enemySprite(const Enemy& e) const;
    void drawCursor();
    bool button(float x, float y, float w, float h, const std::string& label, bool enabled = true, uint32_t col = 0xe8d8a8);
    void panel(float x, float y, float w, float h);
    void bar(float x, float y, float w, float h, float frac, uint32_t col, uint32_t back);
    void computeViewport(float& ox, float& oy, float& scale) const;
};
