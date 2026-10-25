#pragma once
#include "Gfx.hpp"

#include <string>
#include <vector>

constexpr int SKIN_COUNT = 7;
constexpr int WEAPON_COUNT = 5;

struct Skin {
    std::string name, perk, unlock;
    Palette pal;
    uint32_t eye; // glowing lens colour (0xRRGGBB)
};
extern std::vector<Skin> SKINS;

// Emissive rectangles baked into the backdrop (lit windows).
struct Window { float x, y, w, h; uint32_t col; float phase; };
struct Star { float x, y; uint32_t col; float phase; };

// Arm + weapon composite, rotated around the shoulder.
struct ArmGfx { Sprite spr; float px = 0, py = 0, mx = 0, my = 0; };

struct ArtAssets {
    Sprite doctor[SKIN_COUNT][2];
    ArmGfx arm[SKIN_COUNT][WEAPON_COUNT];
    Sprite weaponIcon[WEAPON_COUNT];

    Sprite rat[2], plagueRat[2], giantRat[2], kingRat[2], ratling[2];
    Sprite crow[2], bloater[2], abom[2], spitter[2], brute[2];

    Sprite crown, glob, flask, trapOpen, trapShut, coin[2], heart, plank, skull, lock, tonic, muzzle, sign, cursor, lantern;
    Sprite stake[3], stakeBroken[3], spike[3];
    Sprite lamp;
    Sprite cloud[3];

    SDL_Texture *sky = nullptr, *town = nullptr, *ground = nullptr, *clinic = nullptr;
    SDL_Texture *fog = nullptr, *vignette = nullptr, *light = nullptr, *puff = nullptr, *shadow = nullptr;
    int fogW = 0, fogH = 0;

    std::vector<Window> windows;
    std::vector<Star> stars;
};
extern ArtAssets A;

// Positions inside sprites (outlined pixel coords) used for glowing eyes etc.
constexpr float DOC_EYE_X = 8.0f, DOC_EYE_Y = 9.0f;      // centre of the 2x2 lens
constexpr float DOC_SHOULDER_X = 9.5f, DOC_SHOULDER_Y = 14.5f;

void buildArt();
