#pragma once
#include "Gfx.hpp"

#include <string>
#include <vector>

constexpr int SKIN_COUNT = 7;
constexpr int WEAPON_COUNT = 10;
constexpr int STAGE_COUNT = 4;

struct Skin {
    std::string name, perk, unlock;
    Palette pal;
    uint32_t eye; // glowing lens colour (0xRRGGBB)
};
extern std::vector<Skin> SKINS;

// Emissive rectangles baked into the backdrop (lit windows, lamp glass).
struct Window { float x, y, w, h; uint32_t col; float phase; };
struct Star { float x, y; uint32_t col; float phase; };
// Static light added to the lightmap every frame. flicker in [0,1] is how much it wavers.
struct StageLight { float x, y, r; uint32_t col; float a, flicker; };
// Unlit additive glow drawn after lighting (will-o-wisps, fire glow, lamp halos).
struct StageGlow { float x, y, r; uint32_t col; float a, flicker; };

// One backdrop per stage: 0 Plague Town, 1 Blighted Marsh, 2 Burning Cathedral, 3 Frozen Abbey.
// sky/town/ground are full-width textures: sky and town are VW x VH, ground is VW x (VH - 158) drawn at y=158.
// Any static props (lamp posts, dead trees, gravestones...) are baked into the town texture.
struct StageArt {
    SDL_Texture *sky = nullptr, *town = nullptr, *ground = nullptr;
    std::vector<Window> windows;
    std::vector<Star> stars;
    std::vector<StageLight> lights;
    std::vector<StageGlow> glows;
};

// Arm + weapon composite, rotated around the shoulder.
struct ArmGfx { Sprite spr; float px = 0, py = 0, mx = 0, my = 0; };

struct ArtAssets {
    Sprite doctor[SKIN_COUNT][2];
    ArmGfx arm[SKIN_COUNT][WEAPON_COUNT];
    Sprite weaponIcon[WEAPON_COUNT];

    // Enemies face right (toward the clinic). Two animation frames each.
    Sprite rat[2], plagueRat[2], giantRat[2], kingRat[2], ratling[2];
    Sprite crow[2], bloater[2], abom[2], spitter[2], brute[2];
    Sprite ghoul[2], ghoulLeap;      // hunched graveyard ghoul + mid-air leap pose
    Sprite bat[2];                   // small flyer, centre anchored
    Sprite knight[2];                // plague knight holding a tower shield in front (right side)
    Sprite cultist[2];               // robed plague cultist with a censer staff
    Sprite bishop[2];                // boss: floating plague bishop (centre anchored), tall mitre
    Sprite horseman[2], horsemanRear; // boss: pale rider on a skeletal horse, plus a rearing/stunned pose

    Sprite crown, glob, flask, trapOpen, trapShut, coin[2], heart, plank, skull, lock, tonic, muzzle, sign, cursor, lantern;
    Sprite saw, leech, gasCan, scythe, ping, revive, hammer, star, chevron;
    Sprite stake[3], stakeBroken[3], spike[3];
    Sprite lamp;
    Sprite cloud[3];

    StageArt stage[STAGE_COUNT];
    SDL_Texture *clinic = nullptr;
    SDL_Texture *fog = nullptr, *vignette = nullptr, *light = nullptr, *puff = nullptr, *shadow = nullptr;
    int fogW = 0, fogH = 0;
};
extern ArtAssets A;

// Positions inside sprites (outlined pixel coords) used for glowing eyes etc.
constexpr float DOC_EYE_X = 8.0f, DOC_EYE_Y = 9.0f;      // centre of the 2x2 lens
constexpr float DOC_SHOULDER_X = 9.5f, DOC_SHOULDER_Y = 14.5f;

void buildArt();
