#pragma once
// Tunable game data shared by the logic and render translation units.
#include "Audio.hpp"
#include "Game.hpp"

constexpr float DOC_X = 344.f, DOC_Y = 190.f;
constexpr float BARR_X = 304.f;
constexpr float LANE_TOP = 166.f, LANE_BOT = 198.f;
constexpr float GROUND_Y = 158.f;
constexpr float HUD_COIN_X = VW - 44.f, HUD_COIN_Y = 9.f;
constexpr float LAMP_X[2] = {96.f, 214.f};
constexpr float LAMP_FEET = 166.f;
constexpr int STAKES = 9;

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
    bool flame, hold;
    float knock, shake;
    int sfx;
    uint32_t col;
};
inline const WeaponDef WEAPONS[WEAPON_COUNT] = {
    {"FLINTLOCK", "A TRUSTY PISTOL. AIM HIGH FOR CRITS.", 0, 14, 1, 0.03f, 6, 0.20f, 1.0f, 560, 0, 0, false, false, 40, 1.2f, SFX_PISTOL, 0xffe08a},
    {"BLUNDERBUSS", "7 PELLETS OF POINT-BLANK JUSTICE.", 300, 7, 7, 0.30f, 2, 0.50f, 1.5f, 480, 0, 0, false, false, 70, 3.5f, SFX_BLUNDER, 0xffc070},
    {"REPEATER BOW", "HOLD TO FIRE. BOLTS PIERCE 3 FOES.", 650, 24, 1, 0.015f, 8, 0.26f, 1.6f, 640, 3, 0, false, true, 25, 0.8f, SFX_XBOW, 0xd8e8ff},
    {"BRIMSTONE CENSER", "HOLY FIRE. SHORT RANGE, SETS ABLAZE.", 1000, 2.6f, 1, 0.16f, 100, 0.035f, 2.0f, 210, 99, 0, true, true, 4, 0.3f, SFX_FLAME, 0xff8a30},
    {"HAND CANNON", "ALCHEMICAL SHELLS THAT EXPLODE.", 1500, 40, 1, 0.02f, 3, 0.70f, 1.9f, 400, 0, 28, false, false, 120, 4.5f, SFX_CANNON, 0xa8ff6a},
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
    {"THE RAT KING", 650, 9, 14, 1.4f, 160, 12, 7, 3, false, 0.95f, 0x8a1020},
    {"THE ABOMINATION", 1400, 7, 22, 1.6f, 320, 22, 23, 2, false, 0.97f, 0x9a3030},
};
inline bool isBoss(EType t) { return t == E_RATKING || t == E_ABOM; }

struct UpgradeDef { const char* name; const char* desc; int base, max; };
inline const UpgradeDef UPGRADES[5] = {
    {"POTENCY", "+15% DAMAGE FOR EVERY WEAPON.", 150, 5},
    {"DEXTERITY", "+12% FIRE RATE AND RELOAD SPEED.", 140, 5},
    {"BANDOLIER", "+34% CLIP SIZE FOR EVERY WEAPON.", 200, 3},
    {"VITALITY", "+25 MAX HEALTH (AND HEALS 25).", 180, 3},
    {"FORTIFY", "STRONGER BARRICADE. FULLY REPAIRED.", 400, 2},
};

struct SupplyDef { const char* name; const char* desc; int price, max; };
inline const SupplyDef SUPPLIES[5] = {
    {"REPAIR BARRICADE", "PATCH UP THE STAKES BEFORE THE NEXT WAVE.", 0, 0},
    {"FIRE FLASK", "RMB/F: LOB GREEK FIRE. BIG BLAST + FLAMES.", 45, 9},
    {"BEAR TRAP", "E: SNARES A FOE AT THE CURSOR.", 40, 6},
    {"HEALING TONIC", "Q: DRINK MID-BATTLE TO HEAL 45.", 60, 3},
    {"MEND WOUNDS", "THE DOCTOR TREATS HIMSELF. FULL HEAL.", 0, 0},
};

inline SDL_FRect slotRect(int i) { return SDL_FRect{6.f + i * 25.f, float(VH - 21), 23.f, 16.f}; }
