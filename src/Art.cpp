#include "Art.hpp"
#include "Util.hpp"

#include <array>
#include <cmath>

using namespace util;

ArtAssets A;
std::vector<Skin> SKINS;

namespace {

constexpr uint32_t OUTLINE = argb(0x0d0a12);

Palette pal(std::initializer_list<std::pair<const char, uint32_t>> l) {
    Palette p;
    for (auto& kv : l) p[kv.first] = argb(kv.second);
    return p;
}

Sprite spr(const std::vector<std::string>& rows, const Palette& p) {
    Sprite s = gfx::sprite(outlined(artToImage(rows, p), OUTLINE));
    for (int y = 0; y < int(rows.size()); y++)
        for (int x = 0; x < int(rows[y].size()); x++) {
            char c = rows[y][x];
            if ((c == 'e' || c == 'Y') && p.count(c)) s.glow.push_back({x + 1, y + 1, p.at(c) & 0xFFFFFF, c == 'e'});
        }
    return s;
}

// ============================================================ Characters

// Plague doctor, facing left. Arm is drawn separately.
const std::vector<std::string> DOCTOR = {
    ".......HHHH.....",
    "......HHHHHH....",
    "......HhHHHk....",
    "......HhHHHk....",
    "......BBBBBB....",
    "..HHHHHHHHHHHHH.",
    "......MMMMMCCc..",
    ".....MEEMMMCCc..",
    "...mMMEEMMMCCc..",
    "..mMMMMMMMMMCCc.",
    "mMMMMMMMMMMmCCc.",
    ".mmmmmmMMMmmCCc.",
    "......mmmCCCCCc.",
    ".....CCCCCCCCCc.",
    ".....CDCCCCCCCcc",
    ".....CDCCCCCCCcc",
    ".....LLLbLLLLLLc",
    ".....CDCCCPPCCcc",
    "....CCDCCCPPCCcc",
    "....CCDCCCCCCCcc",
    "....CDCCCCCCCccc",
    "...CCDCCCCCCCccc",
    "...CDCCCCCCCcccc",
    "...DCCCCCCCCcccc",
    ".....SS...SS....",
    "....SSSs.SSSs...",
};

const std::vector<std::string> ARM = {
    "CCCCCGG",
    "ccccGGG",
    "....GG.",
};
constexpr int HAND_X = 5, HAND_Y = 1;

struct WeaponArt { std::vector<std::string> rows; int gx, gy; float my; };
const WeaponArt WEAPON_ART[WEAPON_COUNT] = {
    {{"....O.......",
      "..WWWIIIIIII",
      ".WWWOiiiiiii",
      "WWw.........",
      "Ww.........."}, 3, 2, 2.0f},
    {{"...............OO",
      ".....OIIIIIIIIIOo",
      "WWWWWWWiiiiiiiiOo",
      "WWWwW..........OO",
      "Ww..............."}, 4, 2, 2.0f},
    {{"..........I....",
      "..........I....",
      "....TTTTTTITTTI",
      "WWWWWWWWWWIWWW.",
      "WWwO.....wI....",
      "Ww........I....",
      "..........I...."}, 3, 4, 2.5f},
    {{"....oOOo.......",
      "...oOVVOo......",
      "...OVVVVOIIIIiF",
      "WWWOVVVVOiiiii.",
      "WWwoOVVOo......",
      "Ww..oOOo......."}, 2, 4, 3.0f},
    {{"........OOO.......",
      "......OOVVVO......",
      "WWWWWWIIIIIIIIIIOO",
      "WWWWwOIiiiiiiiiiOO",
      "Www..OO...........",
      "Ww................"}, 4, 3, 3.0f},
};

const Palette WEAPON_PAL = pal({{'I', 0x9aa0aa}, {'i', 0x555a63}, {'W', 0x8a5428}, {'w', 0x553218}, {'O', 0xe0b040},
                                {'o', 0x8f6a1e}, {'T', 0xd8b880}, {'V', 0x7aff7a}, {'F', 0xffa030}});

// Rat family (facing right).
const std::vector<std::string> RAT_A = {
    "........rr....",
    "...RRRRRRRR...",
    ".RRRRRRRRRReR.",
    "TRRRRRRRRRRRRn",
    "T.rllllllRRt..",
    "T..r.r...r.r..",
};
const std::vector<std::string> RAT_B = {
    "........rr....",
    "...RRRRRRRR...",
    ".RRRRRRRRRReR.",
    "TRRRRRRRRRRRRn",
    "T.rllllllRRt..",
    ".Tr...r.r...r.",
};
const std::vector<std::string> RATLING_A = {".RRRRR..", "TRRRRReR", ".lllRRRn", "..r.r.r."};
const std::vector<std::string> RATLING_B = {".RRRRR..", "TRRRRReR", ".lllRRRn", ".r.r.r.."};

const std::vector<std::string> CROW_A = {
    "...QQ.......",
    "....QQq.....",
    ".....QQq.QQ.",
    "..QQQQQQQQey",
    "QQQQqqqQQQ..",
    "...Qq.......",
    "............",
    "............",
};
const std::vector<std::string> CROW_B = {
    "............",
    "............",
    ".........QQ.",
    "..QQQQQQQQey",
    "QQQQqqqQQQ..",
    "....QQq.....",
    ".....QQq....",
    "......Qq....",
};

const std::vector<std::string> BLOATER_TOP = {
    "......ZZZ.......",
    ".....ZZZZZ......",
    ".....ZZZeZ......",
    ".....zZZttZ.....",
    "....UUzZZZUU....",
    "...ZZZZZZZZZZ...",
    "..ZZZZZZZZYZZZ..",
    ".ZZZZYZZZZZZZZZ.",
    ".ZZZZZZZZZZZYZZ.",
    "ZZZYZZZZZZZZZZZz",
    "ZZZZZZZZZZZZZZZz",
    "ZZZZZZZZZYZZZZZz",
    "zZZZZZZZZZZZZZzz",
    ".zZZZYZZZZZZZzz.",
    "..zzZZZZZZZZzz..",
    "...UUUUUUUUUU...",
    "...uUUuUUUuUU...",
};
const std::vector<std::string> BLOATER_LEGS_A = {"....zz....zz....", "....zz....zz....", "...zzz...zzz...."};
const std::vector<std::string> BLOATER_LEGS_B = {".....zz..zz.....", ".....zz..zz.....", "....zzz.zzz....."};

const std::vector<std::string> SPITTER_TOP = {
    ".........ZZZ..",
    "........ZZZeZ.",
    "........ZZZZtt",
    ".......ZZYYZ..",
    "......ZZYZ....",
    "....UUUZZ.....",
    "...UUUUUZZ....",
    "..UUUUUUUZZ...",
    "..UUuUUUUUZz..",
    "..UUUUuUUU.Zz.",
    "..uUUUUUuU..z.",
    "...UUuUUU.....",
};
const std::vector<std::string> SPITTER_LEGS_A = {"...zZ...zZ....", "..zZ.....zZ...", ".zz.......zz.."};
const std::vector<std::string> SPITTER_LEGS_B = {"....zZ.zZ.....", "....zZ.zZ.....", "...zz..zz....."};

const std::vector<std::string> BRUTE_TOP = {
    "........ZZZZ......",
    ".......ZZZZZZ.....",
    ".......ZZZZeZ.....",
    ".......zZZtttZ....",
    ".....UUUZZZZUU....",
    "...UUUUUUUUUUUUU..",
    "..ZUUUUUUUUUUUUUZ.",
    ".ZZUUUuUUUUUuUUUZZ",
    ".ZZUUUUUUUUUUUUUZZ",
    "ZZ.UUUUUuUUUUUU.ZZ",
    "ZZ.uUUUUUUUUUUu.ZY",
    "Zz..UUUUUUUUUU..zZ",
    "zz..LLLLbLLLLL..zz",
    ".z..UUUUUUUUUU..z.",
    "....uUUUuUUUUu....",
};
const std::vector<std::string> BRUTE_LEGS_A = {
    "....ZZZ....ZZZ....", "....ZZz....ZZz....", "....ZZ......ZZ....", "...zZZ.....zZZ....", "..zzzz....zzzz....",
};
const std::vector<std::string> BRUTE_LEGS_B = {
    ".....ZZZ..ZZZ.....", ".....ZZz..ZZz.....", ".....ZZ...ZZ......", "....zZZ..zZZ......", "...zzzz.zzzz......",
};


// Boss: a heaving mass of plague flesh with too many eyes.
const std::vector<std::string> ABOM_TOP = {
    "..........zzzzz...........",
    "........zzZZZZZzz.........",
    ".......zZZZeZZZZZz........",
    "......zZZZZZZZZeZZz.......",
    ".....zZZeZZZZZZZZZZz......",
    "....zZZZZZZZYZZZZZZZz.....",
    "...zZZZZZZZZZZZZZeZZZz....",
    "...zZZYZZZZmmmZZZZZZZz....",
    "..zZZZZZZZmtmtmZZZZZZZz...",
    "..zZZZZZZZmmmmmZZZYZZZz...",
    ".zZZZZeZZZmtmtmZZZZZZZZz..",
    ".zZZZZZZZZZmmmZZZZZZZZZz..",
    "zZZZYZZZZZZZZZZZZZeZZZZZz.",
    "zZZZZZZZZZZZZYZZZZZZZZZZz.",
    "zZZZZZZZZZZZZZZZZZZZZYZZzz",
    "zzZZZZZZYZZZZZZZZZZZZZZzzZ",
    ".zzZZZZZZZZZZZZZZZZZZZzzZZ",
    "..zzzZZZZZZZZZZZZZZZzzz.ZZ",
    "...xzzzzZZZZZZZZZzzzzx..Zz",
    "...xx.zzzzzzzzzzzzz.xx..z.",
};
const std::vector<std::string> ABOM_LEGS_A = {
    "..xx..zz...zz..zz..xx.....",
    ".xx...zz...zz..zz...xx....",
    "xx...zz....zz...zz...xx...",
    "x...zzz...zzz....zzz...x..",
};
const std::vector<std::string> ABOM_LEGS_B = {
    "...xx.zz...zz..zz.xx......",
    "...xx..zz..zz.zz..xx......",
    "..xx...zz.zz..zz...xx.....",
    "..x...zzz.zzz.zzz...x.....",
};

std::vector<std::string> join(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<std::string> r = a;
    r.insert(r.end(), b.begin(), b.end());
    return r;
}

void pair2(Sprite out[2], const std::vector<std::string>& a, const std::vector<std::string>& b, const Palette& p) {
    out[0] = spr(a, p);
    out[1] = spr(b, p);
}

// ============================================================ Skins

void buildSkins() {
    auto mk = [](const char* name, const char* perk, const char* unlock, std::initializer_list<std::pair<const char, uint32_t>> l,
                 uint32_t eye) {
        Skin s;
        s.name = name;
        s.perk = perk;
        s.unlock = unlock;
        s.pal = pal(l);
        s.eye = eye;
        SKINS.push_back(s);
    };
    mk("THE PHYSICIAN", "NO PERK. PURE STYLE.", "",
       {{'H', 0x2c2832}, {'h', 0x4a4452}, {'k', 0x18151c}, {'B', 0x7a2020}, {'M', 0xd8cfb4}, {'m', 0xa39878}, {'E', 0xffb347},
        {'C', 0x37323f}, {'c', 0x211e27}, {'D', 0x564e64}, {'L', 0x5a3b22}, {'b', 0xc9a24a}, {'S', 0x2b1d14}, {'s', 0x1a110b},
        {'G', 0x3b2a1e}, {'P', 0x6d4a2a}},
       0xffb347);
    mk("CRIMSON CARDINAL", "+25% CRITICAL DAMAGE", "REACH WAVE 3",
       {{'H', 0x5e0f16}, {'h', 0x8a1f27}, {'k', 0x3a070c}, {'B', 0xd4a83a}, {'M', 0xf0e6d0}, {'m', 0xbfae8c}, {'E', 0x7fe0ff},
        {'C', 0x8a1622}, {'c', 0x560c14}, {'D', 0xb83040}, {'L', 0x2a1a12}, {'b', 0xf0c850}, {'S', 0x1e1210}, {'s', 0x120a08},
        {'G', 0xe8dcc0}, {'P', 0x3a2418}},
       0x7fe0ff);
    mk("VERDANT ALCHEMIST", "+2 FIRE FLASKS EACH RUN", "SLAY 150 VERMIN",
       {{'H', 0x23402a}, {'h', 0x3d6a45}, {'k', 0x142818}, {'B', 0xb08a3a}, {'M', 0xc0904c}, {'m', 0x7d5a2c}, {'E', 0x7dff6a},
        {'C', 0x2f5a35}, {'c', 0x1c3a22}, {'D', 0x4f8a50}, {'L', 0x4a3020}, {'b', 0xd8b048}, {'S', 0x2a1c12}, {'s', 0x1a110b},
        {'G', 0x5a4030}, {'P', 0x8a6a30}},
       0x7dff6a);
    mk("ASHEN INQUISITOR", "BARRICADE +30% HEALTH", "REACH WAVE 8",
       {{'H', 0x3b3b40}, {'h', 0x5c5c64}, {'k', 0x222226}, {'B', 0x9a9aa0}, {'M', 0x7a7f86}, {'m', 0x4a4d52}, {'E', 0xff8a1a},
        {'C', 0x55555c}, {'c', 0x38383e}, {'D', 0x7a7a82}, {'L', 0x2a2a2e}, {'b', 0xb0b0b8}, {'S', 0x1c1c20}, {'s', 0x111114},
        {'G', 0x2e2e33}, {'P', 0x4a4038}},
       0xff8a1a);
    mk("BONE VENETIAN", "+15% COINS", "SLAY 500 VERMIN",
       {{'H', 0xe8e0cc}, {'h', 0xffffff}, {'k', 0xb8ae98}, {'B', 0xc9a24a}, {'M', 0xe0b850}, {'m', 0xa07a28}, {'E', 0xff4fd8},
        {'C', 0xded6c2}, {'c', 0xa89e88}, {'D', 0xfaf4e6}, {'L', 0x6a4a2a}, {'b', 0xd8b048}, {'S', 0x4a3a2a}, {'s', 0x2e2418},
        {'G', 0xf0e8d8}, {'P', 0x8a6a4a}},
       0xff4fd8);
    mk("MIDNIGHT WRAITH", "WIDER PERFECT RELOAD WINDOW", "REACH WAVE 12",
       {{'H', 0x1a1838}, {'h', 0x2e2a5e}, {'k', 0x0e0c22}, {'B', 0x5ad8ff}, {'M', 0x1c1a26}, {'m', 0x0b0a10}, {'E', 0x4ff0ff},
        {'C', 0x231f4a}, {'c', 0x14112e}, {'D', 0x3a3474}, {'L', 0x2a2448}, {'b', 0x5ad8ff}, {'S', 0x0e0c1c}, {'s', 0x08070f},
        {'G', 0x1e1a3a}, {'P', 0x2e2a50}},
       0x4ff0ff);
    mk("GILDED PLAGUE", "+10% DAMAGE AND +10% COINS", "SLAY 3 BOSSES",
       {{'H', 0xb8862a}, {'h', 0xf0c850}, {'k', 0x7a5418}, {'B', 0x9a1622}, {'M', 0xffd860}, {'m', 0xc89a30}, {'E', 0xff3030},
        {'C', 0xa87a22}, {'c', 0x7a5418}, {'D', 0xe8b840}, {'L', 0x5a2a12}, {'b', 0xff3030}, {'S', 0x4a2a10}, {'s', 0x2e1a0a},
        {'G', 0xd8a838}, {'P', 0x8a5a20}},
       0xff3030);
}

ArmGfx makeArm(const Palette& skinPal, int w) {
    const WeaponArt& wa = WEAPON_ART[w];
    Image arm = artToImage(ARM, skinPal);
    Image wep = artToImage(wa.rows, WEAPON_PAL);
    int wx0 = HAND_X - wa.gx, wy0 = HAND_Y - wa.gy;
    int ox = std::max(0, -wx0), oy = std::max(0, -wy0);
    int W = std::max(ox + arm.w, ox + wx0 + wep.w), H = std::max(oy + arm.h, oy + wy0 + wep.h);
    Image c(W, H);
    c.blit(wep, ox + wx0, oy + wy0);
    c.blit(arm, ox, oy);
    ArmGfx g;
    g.spr = gfx::sprite(outlined(c, OUTLINE));
    g.px = ox + 1 + 1.5f;
    g.py = oy + 1 + 1.5f;
    g.mx = float(ox + wx0 + wep.w + 1);
    g.my = oy + wy0 + wa.my + 1;
    return g;
}

// ============================================================ Backdrop

void buildSky() {
    Image sky(VW, VH);
    struct Stop { float y; uint32_t c; };
    const Stop stops[] = {{0, 0x0a0918}, {50, 0x15132e}, {95, 0x2a1f45}, {125, 0x43305a}, {145, 0x5a4560}, {162, 0x5e5a58}, {216, 0x5e5a58}};
    const float mx = 62, my = 46, mr = 14;
    const float q = 18;
    for (int y = 0; y < VH; y++) {
        int s = 0;
        while (s < 5 && y > stops[s + 1].y) s++;
        float t = (y - stops[s].y) / (stops[s + 1].y - stops[s].y);
        uint32_t base = mixRGB(stops[s].c, stops[s + 1].c, t);
        for (int x = 0; x < VW; x++) {
            float c[3] = {float((base >> 16) & 255), float((base >> 8) & 255), float(base & 255)};
            float d = std::hypot(x - mx, y - my);
            if (d > mr) {
                float h = std::pow(std::max(0.f, 1 - (d - mr) / 60.f), 2.2f) * 0.45f;
                c[0] += (200 - c[0]) * h;
                c[1] += (205 - c[1]) * h;
                c[2] += (230 - c[2]) * h;
            }
            uint32_t out = 0;
            for (int k = 0; k < 3; k++) {
                float v = c[k] / 255.f * q, b = std::floor(v);
                v = (b + ((v - b) > bayer(x, y) ? 1 : 0)) / q * 255.f;
                out |= uint32_t(clampf(v, 0, 255)) << (16 - k * 8);
            }
            sky.set(x, y, argb(out));
        }
    }
    // Stars
    for (int i = 0; i < 110; i++) {
        int x = irnd(0, VW - 1), y = irnd(0, 120);
        if (std::hypot(x - mx, y - my) < 34) continue;
        uint32_t col = i % 3 == 0 ? 0xd0d8ff : (i % 3 == 1 ? 0xfff0c0 : 0xffffff);
        uint32_t a = uint32_t(rnd(70, 220));
        sky.blend(x, y, argb(col, a));
        if (i % 9 == 0) {
            sky.blend(x - 1, y, argb(col, a / 3));
            sky.blend(x + 1, y, argb(col, a / 3));
            sky.blend(x, y - 1, argb(col, a / 3));
            sky.blend(x, y + 1, argb(col, a / 3));
        }
        if (i % 4 == 0) A.stars.push_back({float(x), float(y), col, rnd(0, 6.28f)});
    }
    // Moon: shaded disc + craters, dithered.
    for (int y = int(my - mr - 1); y <= int(my + mr + 1); y++)
        for (int x = int(mx - mr - 1); x <= int(mx + mr + 1); x++) {
            float dx = x + .5f - mx, dy = y + .5f - my;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d > mr) continue;
            float shade = (dx + dy) / (mr * 1.6f) + (d / mr) * 0.35f;
            uint32_t c = shade > bayer(x, y) + 0.25f ? 0xbdb89a : 0xece8cc;
            if (d > mr - 1) c = 0xd8d4b8;
            sky.set(x, y, argb(c));
        }
    struct Crater { float x, y, r; };
    const Crater cr[] = {{-5, -3, 3}, {4, 3, 2.5f}, {-1, 6, 2}, {3, -6, 1.6f}, {-7, 4, 1.4f}, {7, -2, 1.2f}};
    for (auto& c : cr) {
        sky.disc(mx + c.x, my + c.y, c.r, argb(0xc4bf9f));
        sky.disc(mx + c.x + 0.6f, my + c.y + 0.6f, c.r * 0.6f, argb(0xb0ab8c));
    }
    A.sky = gfx::texture(sky);
}

Image makeCloud(int w, int h) {
    Image img(w, h);
    std::vector<std::array<float, 3>> blobs;
    int n = w / 9;
    for (int i = 0; i < n; i++) {
        float bx = (i + 0.5f) * w / float(n) + rnd(-3, 3);
        float br = rnd(h * 0.25f, h * 0.48f) * (1 - std::fabs(bx / w - 0.5f) * 0.8f);
        blobs.push_back({bx, h - br - 1, br});
    }
    auto inside = [&](int x, int y) {
        for (auto& b : blobs) {
            float dx = x + .5f - b[0], dy = (y + .5f - b[1]) * 1.3f;
            if (dx * dx + dy * dy < b[2] * b[2]) return true;
        }
        return y >= h - 3 && x > 4 && x < w - 5;
    };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!inside(x, y)) continue;
            uint32_t c = 0x2a2444;
            if (!inside(x, y - 1)) c = 0x6a6490;
            else if (!inside(x, y - 2) && bayer(x, y) < 0.6f) c = 0x48426a;
            else if (y > h * 0.65f && bayer(x, y) < 0.5f) c = 0x201b36;
            img.set(x, y, argb(c, 235));
        }
    return img;
}

void deadTree(Image& img, float x, float y, float ang, float len, int depth, uint32_t c) {
    float x2 = x + std::cos(ang) * len, y2 = y + std::sin(ang) * len;
    img.line(int(x), int(y), int(x2), int(y2), c);
    if (depth > 3) img.line(int(x) + 1, int(y), int(x2) + 1, int(y2), c);
    if (depth <= 0) return;
    int kids = irnd(2, 3);
    for (int i = 0; i < kids; i++) deadTree(img, x2, y2, ang + rnd(-0.75f, 0.75f), len * rnd(0.55f, 0.75f), depth - 1, c);
}

void buildTown() {
    Image t(VW, VH);
    // Far rooftops
    const uint32_t FC = argb(0x1e1932);
    bool church = false;
    int x = -4;
    while (x < VW) {
        int w = irnd(14, 30), h = irnd(16, 40), base = 160;
        if (!church && x > 130) {
            church = true;
            int tw = 14, th = 62, tx = x;
            t.fill(tx, base - th, tw, th, FC);
            for (int i = 0; i < 26; i++) t.fill(tx + i / 4, base - th - i, tw - (i / 4) * 2, 1, FC);
            t.fill(tx + 6, base - th - 34, 2, 8, FC);
            t.fill(tx + 4, base - th - 31, 6, 2, FC);
            // Rose + stained glass
            t.fill(tx + 5, base - th + 10, 4, 4, argb(0x3a2a40));
            A.windows.push_back({float(tx + 5), float(base - th + 10), 4, 4, 0xff6a5a, 0});
            t.fill(tx + 5, base - th + 24, 4, 10, argb(0x3a2a40));
            A.windows.push_back({float(tx + 5), float(base - th + 24), 2, 10, 0x6ab0ff, 1});
            A.windows.push_back({float(tx + 7), float(base - th + 24), 2, 10, 0xffd05a, 2});
            x += tw + 2;
            continue;
        }
        t.fill(x, base - h, w, h, FC);
        int roof = irnd(0, 2);
        if (roof == 0) {
            for (int i = 0; i < w / 2 + 1; i++) t.fill(x + i, base - h - i, w - i * 2, 1, FC);
        } else if (roof == 1) {
            t.fill(x + 2, base - h - irnd(5, 9), 3, 9, FC);
        } else {
            for (int i = 0; i < w; i++) t.fill(x + i, base - h - i / 3, 1, i / 3 + 1, FC);
        }
        for (int wy = base - h + 4; wy < base - 6; wy += 7)
            for (int wx = x + 3; wx < x + w - 3; wx += 6) {
                if (chance(0.16f)) {
                    t.fill(wx, wy, 2, 3, argb(0x3a3040));
                    A.windows.push_back({float(wx), float(wy), 2, 3, chance(0.7f) ? 0xffc45au : 0xff9a40u, rnd(0, 6.28f)});
                } else if (chance(0.25f)) {
                    t.fill(wx, wy, 2, 3, argb(0x2a2440));
                }
            }
        x += w + irnd(-3, 2);
    }

    // Mid layer: closer silhouettes
    const uint32_t MC = argb(0x110d1c);
    auto house = [&](int hx, int w, int h, int base) {
        t.fill(hx, base - h, w, h, MC);
        for (int i = 0; i < w / 2 + 3; i++) t.fill(hx - 2 + i, base - h - i, w + 4 - i * 2, 1, MC);
        t.fill(hx + w - 7, base - h - w / 2 - 2, 4, 8, MC);
        for (int wy = base - h + 6; wy < base - 8; wy += 12)
            for (int wx = hx + 4; wx < hx + w - 6; wx += 10) {
                if (!chance(0.55f)) continue;
                t.fill(wx, wy, 5, 6, argb(0x3a2e28));
                A.windows.push_back({float(wx), float(wy), 5, 6, 0xffb048, rnd(0, 6.28f)});
                // Mullion cross drawn later as dark lines (via emissive pass gaps)
            }
    };
    house(-6, 44, 50, 166);
    house(232, 36, 38, 166);
    deadTree(t, 70, 166, -PI / 2 - 0.1f, 22, 5, MC);
    deadTree(t, 196, 166, -PI / 2 + 0.15f, 18, 4, MC);
    // Graveyard fence + crosses
    for (int fx = 96; fx < 168; fx += 3) {
        t.fill(fx, 154, 1, 12, MC);
        t.set(fx, 153, MC);
    }
    t.fill(96, 157, 72, 1, MC);
    for (int i = 0; i < 6; i++) {
        int cx = 104 + i * 11 + irnd(-2, 2), ch = irnd(6, 9);
        if (i % 2) {
            t.fill(cx, 166 - ch, 1, ch, MC);
            t.fill(cx - 2, 166 - ch + 2, 5, 1, MC);
        } else {
            t.fill(cx - 2, 166 - ch + 2, 5, ch - 2, MC);
            t.fill(cx - 1, 166 - ch + 1, 3, 1, MC);
        }
    }
    A.town = gfx::texture(t);
}

void buildGround() {
    const int GH = VH - 158;
    Image g(VW, GH, argb(0x1c1b24));
    // Curb
    g.fill(0, 0, VW, 4, argb(0x34303e));
    g.fill(0, 0, VW, 1, argb(0x4a4458));
    g.fill(0, 3, VW, 1, argb(0x16141c));
    for (int x = 0; x < VW; x += irnd(8, 14)) g.set(x, 1, argb(0x22202a)), g.set(x, 2, argb(0x22202a));
    // Cobbles with perspective
    const uint32_t stones[] = {0x2e2d39, 0x34323f, 0x2a2934, 0x383444, 0x302c38};
    int y = 4, row = 0;
    while (y < GH) {
        int rh = 3 + row / 2;
        int x = -irnd(0, rh * 2);
        while (x < VW) {
            int sw = rh * 2 + irnd(-1, 2);
            uint32_t c = stones[irnd(0, 4)];
            g.fill(x + 1, y + 1, sw - 1, rh - 1, argb(c));
            g.fill(x + 1, y + 1, sw - 2, 1, argb(mixRGB(c, 0x6a6480, 0.35f)));
            g.fill(x + 1, y + rh - 1, sw - 1, 1, argb(mixRGB(c, 0x000000, 0.35f)));
            if (chance(0.15f)) g.set(x + irnd(2, sw - 2), y + irnd(1, rh - 1), argb(0x24331f)); // moss
            x += sw;
        }
        y += rh;
        row++;
    }
    // Puddles reflecting the moon
    for (int i = 0; i < 4; i++) {
        float px = rnd(20, 290), py = rnd(16, GH - 8), rx = rnd(8, 16), ry = rx * 0.25f;
        for (int yy = int(py - ry); yy <= int(py + ry); yy++)
            for (int xx = int(px - rx); xx <= int(px + rx); xx++) {
                float dx = (xx - px) / rx, dy = (yy - py) / ry;
                if (dx * dx + dy * dy <= 1) g.set(xx, yy, argb(dy < -0.2f ? 0x3a4668 : 0x2a3350));
            }
        g.fill(int(px - rx * 0.4f), int(py), int(rx * 0.5f), 1, argb(0x8a96c0));
    }
    A.ground = gfx::texture(g);
}

void buildClinic() {
    // Local image placed at (334,100).
    const int W = 50, H = 94;
    Image c(W, H);
    const uint32_t BEAM = argb(0x2a1c14), PLASTER = argb(0x6a5848), PLASTER2 = argb(0x5a4a3c);
    // Walls
    for (int y = 24; y < 80; y++)
        for (int x = 0; x < W; x++) c.set(x, y, bayer(x, y) < 0.2f ? PLASTER2 : PLASTER);
    for (int y : {24, 25, 50, 51, 78, 79}) c.fill(0, y, W, 1, BEAM);
    for (int x : {0, 1, 22, 23, 48, 49}) c.fill(x, 24, 1, 56, BEAM);
    c.line(2, 48, 21, 27, BEAM);
    c.line(2, 49, 21, 28, BEAM);
    c.line(24, 27, 47, 48, BEAM);
    c.line(24, 28, 47, 49, BEAM);
    // Roof sloping up to the right with overhang
    for (int x = 0; x < W; x++) {
        int top = 22 - x * 22 / W;
        for (int y = top; y < 26; y++) c.set(x, y, argb(((y - top) % 3 == 0) ? 0x22161a : 0x3e2a2c));
    }
    c.fill(0, 24, W, 2, argb(0x1a1014));
    // Windows (emissive)
    auto win = [&](int x, int y, int w, int h) {
        c.fill(x - 1, y - 1, w + 2, h + 2, BEAM);
        c.fill(x, y, w, h, argb(0x5a4030));
        A.windows.push_back({float(334 + x), float(100 + y), float(w), float(h), 0xffb050, rnd(0, 6.28f)});
    };
    win(6, 31, 11, 12);
    win(28, 31, 12, 12);
    win(5, 56, 12, 13);
    // Door
    c.fill(26, 54, 16, 26, BEAM);
    for (int x = 27; x < 41; x++)
        for (int y = 55; y < 80; y++) c.set(x, y, argb((x - 27) % 4 == 0 ? 0x2a1a0e : 0x4a301a));
    c.fill(27, 59, 14, 1, argb(0x50525c));
    c.fill(27, 73, 14, 1, argb(0x50525c));
    c.set(38, 66, argb(0xc9a24a));
    c.set(38, 67, argb(0xc9a24a));
    // Foundation
    for (int y = 80; y < H; y++)
        for (int x = 0; x < W; x++) {
            bool mortar = (y - 80) % 5 == 4 || ((x + ((y - 80) / 5) * 4) % 9 == 0);
            c.set(x, y, argb(mortar ? 0x1e1c26 : 0x3e3a48));
        }
    // Porch steps
    c.fill(24, 86, 20, 3, argb(0x5a3e24));
    c.fill(22, 89, 24, 5, argb(0x4a3220));
    c.fill(24, 86, 20, 1, argb(0x7a5a38));
    c.fill(22, 89, 24, 1, argb(0x6a4a2c));
    // Wall bracket for the lantern and sign
    c.fill(0, 44, 4, 1, BEAM);
    A.clinic = gfx::texture(c);
}

Image makeStake(int tier, bool broken) {
    int h = broken ? 10 : 26;
    Image s(5, h);
    uint32_t c0 = 0x8a5a34, c1 = 0x6a4224, c2 = 0x48291a;
    if (tier == 2) c0 = 0x9aa0aa, c1 = 0x6e737c, c2 = 0x484c55;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < 5; x++) {
            if (!broken) {
                if (y == 0 && x != 2) continue;
                if (y == 1 && (x == 0 || x == 4)) continue;
            } else {
                if (y == 0 && (x == 1 || x == 2 || x == 4)) continue;
                if (y == 1 && x == 2) continue;
            }
            s.set(x, y, argb(x <= 1 ? c0 : (x == 2 ? c1 : c2)));
        }
    if (tier >= 1) {
        uint32_t band = tier == 1 ? 0x6a6e78 : 0x3a3d45;
        for (int by : {broken ? 5 : 8, broken ? -1 : 18}) {
            if (by < 0) continue;
            s.fill(0, by, 5, 2, argb(band));
            s.set(1, by, argb(0xc0c4cc));
        }
    }
    if (!broken)
        for (int y = 10; y < h; y += 7) s.set(3, y, argb(c2 - 0x080808));
    return outlined(s, OUTLINE);
}

Image makeSpike(int tier) {
    // Sharpened log angled toward the horde (cheval-de-frise).
    Image s(14, 8);
    uint32_t top = tier == 2 ? 0x9aa0aa : 0x8a5a34, bot = tier == 2 ? 0x555a63 : 0x48291a;
    uint32_t tip = tier == 2 ? 0xe0e4ec : 0xd8b07a;
    for (int i = 0; i <= 13; i++) {
        int y = 7 - i * 7 / 13;
        int x = 13 - i;
        s.set(x, y, argb(i > 10 ? tip : top));
        if (i < 12) s.set(x, y + 1 > 7 ? 7 : y + 1, argb(bot));
        if (tier == 1 && (i == 5 || i == 6)) s.set(x, y, argb(0x6a6e78));
    }
    return outlined(s, OUTLINE);
}

void buildProps() {
    // Lamp post: iron pole, arm and lantern head.
    {
        Image l(9, 52);
        l.fill(3, 8, 2, 42, argb(0x26242e));
        l.fill(3, 8, 1, 42, argb(0x3a3846));
        l.fill(1, 48, 6, 4, argb(0x26242e));
        l.fill(2, 46, 4, 2, argb(0x2e2c38));
        l.fill(1, 0, 7, 2, argb(0x1e1c26));
        l.fill(2, 2, 5, 6, argb(0xffd890));
        l.fill(2, 2, 1, 6, argb(0x1e1c26));
        l.fill(6, 2, 1, 6, argb(0x1e1c26));
        l.fill(4, 2, 1, 6, argb(0x1e1c26));
        l.fill(2, 8, 5, 1, argb(0x1e1c26));
        A.lamp = gfx::sprite(outlined(l, OUTLINE));
    }
    A.lantern = spr({".kkk.", "kYYYk", "kYWYk", "kYYYk", ".kkk."}, pal({{'k', 0x1e1c26}, {'Y', 0xffb050}, {'W', 0xfff0c0}}));
    A.sign = spr({"...kk...", "..k..k..", "WWWWWWWWWWWW", "WwwwwVVwwwwW", "WwwwVvvVwwwW", "WwwVVVVVVwwW",
                  "WwwVVVVVVwwW", "WwwwVVVVwwwW", "WWWWWWWWWWWW"},
                 pal({{'k', 0x30303a}, {'W', 0x5a3a20}, {'w', 0x3e2614}, {'V', 0x6aff7a}, {'v', 0xd0ffd0}}));
    for (int t = 0; t < 3; t++) {
        A.stake[t] = gfx::sprite(makeStake(t, false));
        A.stakeBroken[t] = gfx::sprite(makeStake(t, true));
        A.spike[t] = gfx::sprite(makeSpike(t));
    }
    A.crown = spr({"O..O..O", "OO.O.OO", "OOOOOOO", "OROGORO"}, pal({{'O', 0xf0c850}, {'R', 0xff2040}, {'G', 0x40ff80}}));
    A.glob = spr({".VV.", "VVWV", "VVVV", ".VV."}, pal({{'V', 0xa8ff3a}, {'W', 0xf0ffd0}}));
    A.flask = spr({"..TT..", "..II..", ".VVVV.", "VWVVVV", "VVVVVV", "VVVVvV", ".VVVV."},
                  pal({{'T', 0xc8a070}, {'I', 0xb0c0c8}, {'V', 0xff7a2a}, {'W', 0xffe0a0}, {'v', 0xc04010}}));
    A.trapOpen = spr({"I..........I", "II.I.I.I.III", "IIIIIIIIIIII", "..iiiiiiii.."}, pal({{'I', 0x9aa0aa}, {'i', 0x555a63}}));
    A.trapShut = spr({".I.I.I.I.", "IIIIIIIII", "iiiiiiiii"}, pal({{'I', 0x9aa0aa}, {'i', 0x555a63}}));
    A.coin[0] = spr({".OOO.", "OhOOO", "OhOoO", "OOOoO", ".OOO."}, pal({{'O', 0xf0c040}, {'h', 0xfff4b0}, {'o', 0xb08020}}));
    A.coin[1] = spr({".O.", "OhO", "OOO", "OoO", ".O."}, pal({{'O', 0xf0c040}, {'h', 0xfff4b0}, {'o', 0xb08020}}));
    A.heart = spr({".HH.HH.", "HhHHHHH", "HHHHHHH", ".HHHHH.", "..HHH..", "...H..."}, pal({{'H', 0xe02a3a}, {'h', 0xffb0b0}}));
    A.plank = spr({"WWWWWWW", "wwwwwww", ".W...W.", "WWWWWWW", "wwwwwww"}, pal({{'W', 0x9a6a3a}, {'w', 0x5a3a20}}));
    A.skull = spr({".SSS.", "SSSSS", "S.S.S", "SSSSS", ".S.S."}, pal({{'S', 0xe8e0cc}}));
    A.lock = spr({".III.", "I...I", "IIIII", "IIoII", "IIIII"}, pal({{'I', 0xb0b0b8}, {'o', 0x30303a}}));
    A.tonic = spr({"..T..", ".III.", ".RRR.", "RRRRR", "RhRRR", "RRRRR", ".RRR."},
                  pal({{'T', 0xc8a070}, {'I', 0xb0c0c8}, {'R', 0xd0204a}, {'h', 0xffa0c0}}));
    A.muzzle = gfx::sprite(artToImage({"..Y.....", "...YY...", "YYWWWYY.", "YWWWWWWY", "YYWWWYY.", "...YY...", "..Y....."},
                                      pal({{'Y', 0xffa030}, {'W', 0xfff4c0}})));
    A.cursor = spr({"W.....", "WW....", "WkW...", "WkkW..", "WkkkW.", "WkWWWW", "WW....", "W....."},
                   pal({{'W', 0xffffff}, {'k', 0xc8c0d8}}));
    for (int i = 0; i < 3; i++) A.cloud[i] = gfx::sprite(makeCloud(irnd(60, 96), irnd(14, 20)));
}

float vnoise(float x, float y, int period, uint32_t seed) {
    auto h = [&](int ix, int iy) {
        ix = ((ix % period) + period) % period;
        iy = ((iy % period) + period) % period;
        uint32_t n = uint32_t(ix) * 374761393u + uint32_t(iy) * 668265263u + seed * 2246822519u;
        n = (n ^ (n >> 13)) * 1274126177u;
        return float(n & 0xFFFF) / 65535.f;
    };
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = smooth(x - ix), fy = smooth(y - iy);
    return lerp(lerp(h(ix, iy), h(ix + 1, iy), fx), lerp(h(ix, iy + 1), h(ix + 1, iy + 1), fx), fy);
}

void buildFx() {
    // Tileable dithered fog
    const int FW = 128, FH = 48;
    Image f(FW, FH);
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++) {
            float n = vnoise(x / 16.f, y / 16.f, 8, 1) * 0.65f + vnoise(x / 8.f, y / 8.f, 16, 2) * 0.35f;
            float edge = std::sin(PI * y / FH);
            float a = clampf((n - 0.35f) * 2.2f, 0, 1) * edge;
            float lv = std::floor(a * 4 + bayer(x, y)) / 4.f;
            f.set(x, y, argb(0xffffff, uint32_t(lv * 150)));
        }
    A.fog = gfx::texture(f);
    A.fogW = FW;
    A.fogH = FH;

    // Dithered vignette
    Image v(VW, VH);
    for (int y = 0; y < VH; y++)
        for (int x = 0; x < VW; x++) {
            float dx = (x - VW / 2.f) / (VW / 2.f), dy = (y - VH / 2.f) / (VH / 2.f);
            float d = std::sqrt(dx * dx * 0.8f + dy * dy * 1.1f);
            float a = smooth((d - 0.6f) / 0.6f);
            float lv = std::floor(a * 6 + bayer(x, y)) / 6.f;
            v.set(x, y, argb(0x05030a, uint32_t(lv * 210)));
        }
    A.vignette = gfx::texture(v);

    // Radial light (additive) and soft puff (alpha)
    Image l(64, 64), p(32, 32);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float d = std::hypot(x + .5f - 32, y + .5f - 32) / 32.f;
            float vv = smooth(1 - d);
            vv *= vv;
            uint32_t c = uint32_t(vv * 255);
            l.set(x, y, argb((c << 16) | (c << 8) | c));
        }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            float d = std::hypot(x + .5f - 16, y + .5f - 16) / 16.f;
            p.set(x, y, argb(0xffffff, uint32_t(smooth(1 - d) * 255)));
        }
    A.light = gfx::texture(l, SDL_BLENDMODE_ADD, true);
    A.puff = gfx::texture(p, SDL_BLENDMODE_BLEND, true);

    Image s(16, 6);
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 16; x++) {
            float dx = (x + .5f - 8) / 8, dy = (y + .5f - 3) / 3;
            if (dx * dx + dy * dy <= 1) s.set(x, y, argb(0x000000, 120));
        }
    A.shadow = gfx::texture(s);
}

} // namespace

void buildArt() {
    rngState = 0xC0FFEEu; // deterministic town
    buildSkins();

    for (int s = 0; s < SKIN_COUNT; s++) {
        Image body = artToImage(DOCTOR, SKINS[s].pal);
        A.doctor[s][0] = gfx::sprite(outlined(body, OUTLINE));
        A.doctor[s][1] = gfx::sprite(outlined(bobFrame(body, 16), OUTLINE));
        for (int w = 0; w < WEAPON_COUNT; w++) A.arm[s][w] = makeArm(SKINS[s].pal, w);
    }
    for (int w = 0; w < WEAPON_COUNT; w++) A.weaponIcon[w] = gfx::sprite(outlined(artToImage(WEAPON_ART[w].rows, WEAPON_PAL), OUTLINE));

    Palette ratP = pal({{'R', 0x6b5a52}, {'r', 0x40342f}, {'l', 0x9a8a7a}, {'T', 0xd88a8a}, {'e', 0xff2a2a}, {'n', 0xff9a9a}, {'t', 0xffffff}});
    Palette plagueP = pal({{'R', 0x5f7a42}, {'r', 0x3a4f28}, {'l', 0x9ab070}, {'T', 0xb0c080}, {'e', 0xd8ff3a}, {'n', 0xc0d890}, {'t', 0xffffff}});
    Palette giantP = pal({{'R', 0x4a3a34}, {'r', 0x2a201c}, {'l', 0x6a5a4c}, {'T', 0xc87a7a}, {'e', 0xffa020}, {'n', 0xe08a8a}, {'t', 0xffffff}});
    Palette kingP = pal({{'R', 0x5a4a6a}, {'r', 0x3a2e48}, {'l', 0x8a7a9a}, {'T', 0xd88a9a}, {'e', 0xff2a2a}, {'n', 0xe08aa0}, {'t', 0xffffff}});
    pair2(A.rat, RAT_A, RAT_B, ratP);
    pair2(A.plagueRat, RAT_A, RAT_B, plagueP);
    pair2(A.giantRat, RAT_A, RAT_B, giantP);
    pair2(A.kingRat, RAT_A, RAT_B, kingP);
    pair2(A.ratling, RATLING_A, RATLING_B, ratP);
    pair2(A.crow, CROW_A, CROW_B, pal({{'Q', 0x1a1824}, {'q', 0x3a3a6a}, {'e', 0xff2a2a}, {'y', 0x9a9080}}));

    Palette mutP = pal({{'Z', 0x7d8f5a}, {'z', 0x4f5c3a}, {'Y', 0xd8ff5a}, {'U', 0x5a4632}, {'u', 0x3a2c1e}, {'e', 0xff3a1a},
                        {'t', 0xe8dcb0}, {'L', 0x2a1e14}, {'b', 0x9aa0aa}});
    Palette abomP = pal({{'Z', 0x9a5a5a}, {'z', 0x6a3a3a}, {'x', 0x4a2428}, {'Y', 0xffc040}, {'e', 0xff3030}, {'t', 0xe8dcb0},
                         {'m', 0x2a0a10}});
    pair2(A.bloater, join(BLOATER_TOP, BLOATER_LEGS_A), join(BLOATER_TOP, BLOATER_LEGS_B), mutP);
    pair2(A.abom, join(ABOM_TOP, ABOM_LEGS_A), join(ABOM_TOP, ABOM_LEGS_B), abomP);
    pair2(A.spitter, join(SPITTER_TOP, SPITTER_LEGS_A), join(SPITTER_TOP, SPITTER_LEGS_B), mutP);
    pair2(A.brute, join(BRUTE_TOP, BRUTE_LEGS_A), join(BRUTE_TOP, BRUTE_LEGS_B), mutP);

    buildSky();
    buildTown();
    buildGround();
    buildClinic();
    buildProps();
    buildFx();
    rngState = 0x9E3779B9u ^ uint32_t(SDL_GetTicksNS());
}
