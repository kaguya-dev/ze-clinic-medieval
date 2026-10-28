#pragma once
// Tunable game data shared by the logic and render translation units.
#include "Audio.hpp"
#include "Game.hpp"

constexpr float BARR_X = 304.f;
constexpr float LANE_TOP = 166.f, LANE_BOT = 198.f;
constexpr float GROUND_Y = 158.f;
constexpr float HUD_COIN_X = VW - 44.f, HUD_COIN_Y = 9.f;
constexpr int STAKES = 9;
// The doctors' yard between the barricade and the clinic porch.
constexpr float YARD_X0 = 314.f, YARD_X1 = 378.f, YARD_Y0 = 170.f, YARD_Y1 = 198.f;
constexpr float SPAWN_X[MAX_SLOTS] = {346, 330, 364, 338};
constexpr float SPAWN_Y[MAX_SLOTS] = {190, 178, 182, 196};
constexpr uint32_t SLOT_COL[MAX_SLOTS] = {0xffd860, 0x5ad8ff, 0x8aff6a, 0xff7ad8};

enum WeaponKind { K_BULLET, K_FLAME, K_LIGHTNING, K_SAW, K_GAS, K_LEECH };

struct WeaponDef {
    const char* name;
    const char* desc;
    int price;
    float dmg;
    int pellets;
    float spread;
    int clip;
    float interval, reload, speed;
    int pierce;
    float blast;
    int kind;
    bool hold;
    float knock, shake;
    int sfx;
    uint32_t col;
};
inline const WeaponDef WEAPONS[WEAPON_COUNT] = {
    {"FLINTLOCK", "A TRUSTY PISTOL. AIM HIGH FOR CRITS.", 0, 14, 1, 0.03f, 6, 0.20f, 1.0f, 560, 0, 0, K_BULLET, false, 40, 1.2f, SFX_PISTOL, 0xffe08a},
    {"BLUNDERBUSS", "7 PELLETS OF POINT-BLANK JUSTICE.", 300, 7, 7, 0.30f, 2, 0.50f, 1.5f, 480, 0, 0, K_BULLET, false, 70, 3.5f, SFX_BLUNDER, 0xffc070},
    {"REPEATER BOW", "HOLD TO FIRE. BOLTS PIERCE 3 FOES.", 650, 24, 1, 0.015f, 8, 0.26f, 1.6f, 640, 3, 0, K_BULLET, true, 25, 0.8f, SFX_XBOW, 0xd8e8ff},
    {"BRIMSTONE CENSER", "HOLY FIRE. SHORT RANGE, SETS ABLAZE.", 1000, 2.6f, 1, 0.16f, 100, 0.035f, 2.0f, 210, 99, 0, K_FLAME, true, 4, 0.3f, SFX_FLAME, 0xff8a30},
    {"HAND CANNON", "ALCHEMICAL SHELLS THAT EXPLODE.", 1500, 40, 1, 0.02f, 3, 0.70f, 1.9f, 400, 0, 28, K_BULLET, false, 120, 4.5f, SFX_CANNON, 0xa8ff6a},
    {"ARQUEBUS", "PIERCES EVERY FOE IN A LINE.", 900, 85, 1, 0.004f, 4, 0.85f, 2.2f, 1100, 99, 0, K_BULLET, false, 160, 5.f, SFX_SNIPE, 0xfff0d0},
    {"STORM ROD", "HOLD. LIGHTNING CHAINS BETWEEN FOES.", 1300, 15, 1, 0, 14, 0.22f, 2.0f, 0, 4, 0, K_LIGHTNING, true, 10, 1.f, SFX_ZAP, 0x8ad8ff},
    {"BONESAW LAUNCHER", "SAWS BOUNCE ALONG THE GROUND. GO UNDER SHIELDS.", 1100, 26, 1, 0.03f, 5, 0.50f, 1.8f, 330, 6, 0, K_SAW, false, 30, 2.f, SFX_SAW, 0xe0e0e8},
    {"MIASMA BELLOWS", "LOBS CANISTERS OF CHOKING, SLOWING GAS.", 1200, 22, 1, 0, 3, 0.75f, 2.1f, 0, 0, 30, K_GAS, false, 0, 1.5f, SFX_GAS, 0x9aff5a},
    {"LEECH LAUNCHER", "HOLD. HITS HEAL YOU AND YOUR WOUNDED ALLIES.", 800, 9, 1, 0.05f, 24, 0.10f, 1.7f, 600, 0, 0, K_LEECH, true, 8, 0.4f, SFX_LEECH, 0xff5a7a},
};

struct EnemyDef {
    const char* name;
    float hp, speed, dmg, atkInt;
    int coins;
    float w, h, scale;
    bool fly;
    float knockRes;
    uint32_t blood;
};
inline const EnemyDef EDEF[E_COUNT] = {
    {"RAT", 20, 34, 5, 0.8f, 4, 12, 7, 1, false, 0, 0x8a1020},
    {"RATLING", 6, 52, 2, 0.6f, 1, 7, 4, 1, false, 0, 0x8a1020},
    {"PLAGUE RAT", 28, 30, 6, 0.8f, 5, 12, 7, 1, false, 0, 0x7aa020},
    {"GIANT RAT", 120, 22, 14, 1.0f, 14, 12, 7, 2, false, 0.6f, 0x8a1020},
    {"PLAGUE CROW", 14, 70, 8, 0, 6, 10, 6, 1, true, 0, 0x5a1020},
    {"BLOATER", 70, 13, 0, 0, 10, 14, 19, 1, false, 0.4f, 0x6a9a20},
    {"SPITTER", 45, 26, 0, 2.8f, 9, 10, 15, 1, false, 0.2f, 0x6a9a20},
    {"BRUTE", 240, 16, 22, 1.4f, 22, 14, 20, 1, false, 0.85f, 0x6a9a20},
    {"GHOUL", 55, 40, 9, 0.7f, 9, 14, 12, 1, false, 0.2f, 0x6a3030},
    {"PLAGUE BAT", 8, 95, 5, 0, 3, 10, 7, 1, true, 0, 0x5a1020},
    {"PLAGUE KNIGHT", 170, 18, 18, 1.2f, 20, 13, 20, 1, false, 0.8f, 0x8a1020},
    {"CULTIST", 60, 22, 6, 1.0f, 15, 10, 18, 1, false, 0.3f, 0x8a1020},
    {"THE RAT KING", 650, 9, 14, 1.4f, 160, 12, 7, 3, false, 0.95f, 0x8a1020},
    {"THE ABOMINATION", 1400, 7, 22, 1.6f, 320, 22, 23, 2, false, 0.97f, 0x9a3030},
    {"THE PLAGUE BISHOP", 2400, 0, 25, 0, 480, 20, 32, 2, true, 1.f, 0xc03040},
    {"THE PALE HORSEMAN", 3400, 30, 30, 1.5f, 640, 32, 26, 2, false, 1.f, 0xd8d0c0},
};
inline bool isBoss(EType t) { return t >= E_RATKING; }

struct StageDef {
    const char* name;
    const char* sub;
    EType boss;
    uint32_t ambient; // lightmap clear colour
    int weather;      // 0 rain (sometimes), 1 marsh motes, 2 falling embers, 3 snow
    int ambience;     // Ambience id
    int music;        // music track
    uint32_t fog;
    EType favored[3]; // spawn weight x2.5 on this stage (E_COUNT = unused)
};
inline const StageDef STAGES[STAGE_COUNT] = {
    {"PLAGUE TOWN", "THE STREETS ARE CRAWLING", E_RATKING, 0x6e6a98, 0, AMB_NONE, 1, 0xa8c0a0, {E_RAT, E_CROW, E_GIANTRAT}},
    {"BLIGHTED MARSH", "SOMETHING STIRS IN THE BOG", E_ABOM, 0x587a74, 1, AMB_MARSH, 2, 0x90c8a0, {E_GHOUL, E_BAT, E_BLOATER}},
    {"BURNING CATHEDRAL", "THE FAITHFUL HAVE TURNED", E_BISHOP, 0x8a5a58, 2, AMB_FIRE, 3, 0xc0a090, {E_KNIGHT, E_CULTIST, E_SPITTER}},
    {"FROZEN ABBEY", "A PALE RIDER APPROACHES", E_HORSEMAN, 0x7684b0, 3, AMB_WIND, 4, 0xd0dcf0, {E_BRUTE, E_KNIGHT, E_GHOUL}},
};
// Coins handed out when a run begins at a later stage, so the doctors can arm up first.
inline const int STAGE_START_COINS[STAGE_COUNT] = {0, 1100, 3400, 6500};

struct UpgradeDef { const char* name; const char* desc; int base, max; };
inline const UpgradeDef UPGRADES[5] = {
    {"POTENCY", "+15% DAMAGE FOR EVERY WEAPON.", 150, 5},
    {"DEXTERITY", "+12% FIRE RATE AND RELOAD SPEED.", 140, 5},
    {"BANDOLIER", "+34% CLIP SIZE FOR EVERY WEAPON.", 200, 3},
    {"VITALITY", "+25 MAX HEALTH (AND HEALS 25).", 180, 3},
    {"FORTIFY", "STRONGER BARRICADE FOR THE WHOLE TEAM. REPAIRED.", 400, 2},
};

struct SupplyDef { const char* name; const char* desc; int price, max; };
inline const SupplyDef SUPPLIES[5] = {
    {"REPAIR BARRICADE", "PATCH UP THE STAKES. HOLD F AT THE STAKES MID-WAVE TOO.", 0, 0},
    {"FIRE FLASK", "RMB/G: LOB GREEK FIRE. BIG BLAST + FLAMES.", 45, 9},
    {"BEAR TRAP", "E: SNARES A FOE AT THE CURSOR.", 40, 6},
    {"HEALING TONIC", "Q: DRINK MID-BATTLE TO HEAL 45.", 60, 3},
    {"MEND WOUNDS", "THE DOCTOR TREATS HIMSELF. FULL HEAL.", 0, 0},
};

inline SDL_FRect slotRect(int i) { return SDL_FRect{4.f + i * 23.f, float(VH - 20), 22.f, 16.f}; }
