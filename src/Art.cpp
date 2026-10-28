#include "Art.hpp"
#include "Util.hpp"

#include <array>
#include <cmath>
#include <functional>

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
    // 0 Flintlock
    {{"....O.......",
      "..WWWIIIIIII",
      ".WWWOiiiiiii",
      "WWw.........",
      "Ww.........."}, 3, 2, 2.0f},
    // 1 Blunderbuss
    {{"...............OO",
      ".....OIIIIIIIIIOo",
      "WWWWWWWiiiiiiiiOo",
      "WWWwW..........OO",
      "Ww..............."}, 4, 2, 2.0f},
    // 2 Repeater bow
    {{"..........I....",
      "..........I....",
      "....TTTTTTITTTI",
      "WWWWWWWWWWIWWW.",
      "WWwO.....wI....",
      "Ww........I....",
      "..........I...."}, 3, 4, 2.5f},
    // 3 Brimstone censer
    {{"....oOOo.......",
      "...oOVVOo......",
      "...OVVVVOIIIIiF",
      "WWWOVVVVOiiiii.",
      "WWwoOVVOo......",
      "Ww..oOOo......."}, 2, 4, 3.0f},
    // 4 Hand cannon
    {{"........OOO.......",
      "......OOVVVO......",
      "WWWWWWIIIIIIIIIIOO",
      "WWWWwOIiiiiiiiiiOO",
      "Www..OO...........",
      "Ww................"}, 4, 3, 3.0f},
    // 5 Arquebus: long matchlock with a smouldering match on the serpentine.
    {{"...F.......................",
      "...oO.................O....",
      "..WWWWWIIIIIIIIIIIIIIIIIIIIO",
      "WWWWWWWWWWWWWWWWWWWwiiiiiiiO",
      "WWwwOwwwwwwwwwwwwwww........",
      "Ww.........................."}, 4, 3, 2.5f},
    // 6 Storm rod: iron rod wound with copper, a charged crystal at the tip.
    {{"...........K.K..hC..",
      "......K.K.KkKk.cCCC.",
      "WWWWIIKiKiKiKiIcCHCC",
      "WWwwi.k.k.kKkK.cCCc.",
      "Ww.............cc...",
      "................c..."}, 3, 2, 2.5f},
    // 7 Bonesaw launcher: crossbow stock with a toothed blade on the rail.
    {{"..............I..",
      "......S.S.S...Ii.",
      ".....SsSSSsS..I..",
      "WWWWWsSSoSSsWWIW.",
      "WWwOWSsSSSsS..I..",
      "Ww....S.S.S...Ii.",
      "..............I.."}, 3, 3, 3.5f},
    // 8 Miasma bellows: leather bellows pumping a gas canister through a brass nozzle.
    {{".......gVVg........",
      ".......VAVV........",
      "..LlLlLgVVgo.......",
      "WWLLLLLLLLLOOOOOOOO",
      "WWLlLlLlLlLoooooOOo",
      "Ww.lLlLlLl........."}, 3, 3, 3.5f},
    // 9 Leech launcher: glass tube of leeches feeding a syringe needle.
    {{"....OoGGGGGGoO......",
      "....OGRRgGRRGO......",
      "WWWWOGgRRgRRgOOINNNN",
      "WWwOOGGRRggRGO......",
      "Ww..OoGGGGGGoO......"}, 3, 2, 2.5f},
};

const Palette WEAPON_PAL = pal({{'I', 0x9aa0aa}, {'i', 0x555a63}, {'W', 0x8a5428}, {'w', 0x553218}, {'O', 0xe0b040},
                                {'o', 0x8f6a1e}, {'T', 0xd8b880}, {'V', 0x7aff7a}, {'F', 0xffa030}, {'K', 0xc87a3a},
                                {'k', 0x7a4220}, {'C', 0x6ad0ff}, {'c', 0x2a70c0}, {'H', 0xe8fcff}, {'h', 0x3a80d0},
                                {'S', 0xd8dce4}, {'s', 0x8a8e98}, {'L', 0x8a5a36}, {'l', 0x55331c}, {'g', 0x4a4e58},
                                {'A', 0xd8ffd0}, {'G', 0xb8e0e8}, {'R', 0xc81e36}, {'N', 0xeef2f8}});

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

// Graveyard ghoul: hunched corpse eater dragging its long claws.
const std::vector<std::string> GHOUL_TOP = {
    "......gGGg.....",
    "....gGGHHGGg...",
    "...gGHGgGHGGg..",
    "..gGGGGGGGgGGGg",
    ".gGgGGGGGGGGkeG",
    ".gGGGGgGGdgGGGt",
    "gGGrrGGGd.dGmtm",
};
const std::vector<std::string> GHOUL_LEGS_A = {
    "dgGrrrGd..gG...",
    ".dGGgGd...gG...",
    "..dGGgG...dGg..",
    "..dg.dGg...dGc.",
    ".dd...dd...cccc",
};
const std::vector<std::string> GHOUL_LEGS_B = {
    "dgGrrrGd...gG..",
    ".dGGgGd.....gG.",
    "..dGgGg.....dGg",
    ".dGd.dGg.....cc",
    "dd....ddd...c.c",
};
const std::vector<std::string> GHOUL_LEAP = {
    ".........gGGGg.....",
    "....gGGHHGGHHGGg...",
    "..gGGGGgGGGGGGkeGg.",
    "ddGGGGrrrGGGGGGGGmt",
    "d..dGrrrrGg..gGGcc.",
    "....dg.......gGgccc",
    "...dd..........c..c",
};

// Plague bat (front view, centre anchored).
const std::vector<std::string> BAT_A = {
    "W..b..b..W",
    "WW.bbbb.WW",
    "WWWbeebWWW",
    ".WWbbbbWW.",
    "..W.bb.W..",
    ".....b....",
};
const std::vector<std::string> BAT_B = {
    "...b..b...",
    "...bbbb...",
    ".WWbeebWW.",
    "WWWWbbWWWW",
    "WW..bb..WW",
    "W........W",
};

// Plague knight: beaked great helm, tower shield held in front.
const std::vector<std::string> KNIGHT_TOP = {
    ".....PP.......",
    "....PAAa......",
    "....AhAAa.....",
    "....AAeeAb....",
    "....AAAAAbbb..",
    "....aAAAa.....",
    "...cAAAAA.III.",
    "..ccAhAAAIISSI",
    "..cAAAAAAISSSI",
    "..cAhAAAAISCSI",
    "..cAAAAAAICCCI",
    "..cAhAAAAISCSI",
    "..caAAAAAISCSI",
    "..caLLbLLISSSI",
    "..caAAAAAISSSI",
};
const std::vector<std::string> KNIGHT_LEGS_A = {
    "..c.AAaAAISSSI",
    "....AA.aAISSSI",
    "...AA...aIIII.",
    "...aA....aA...",
    "..aaa....aaa..",
};
const std::vector<std::string> KNIGHT_LEGS_B = {
    "..c.AAaAAISSSI",
    "....AAaA.ISSSI",
    ".....AAa.IIII.",
    ".....aAa......",
    "....aaaaa.....",
};

// Plague cultist: hooded robe, censer swinging from a crook staff.
const std::vector<std::string> CULTIST_A = {
    "...hHHh..WW...",
    "..hHHHHh.W.c..",
    "..HHHHkkhW.c..",
    ".hHHHkkekW.c..",
    ".hHHHHkkhW.I..",
    ".hHHHHHHhWIYI.",
    "hHHHHHHHHWiIi.",
    "hHHHHHHHsW....",
    "hHHHHHHHHW....",
    "hHHHbbbbhW....",
    "hHHHHHHHHW....",
    "hHHHHHHHHW....",
    "hHHHhHHHHW....",
    "hHHHHHhHHW....",
    "hHHHHHHHHHW...",
    "hhHHhHHHHHW...",
    ".hhh.hhhh.W...",
    "..ss...ss.....",
};
const std::vector<std::string> CULTIST_B = {
    "...hHHh..WW...",
    "..hHHHHh.W..c.",
    "..HHHHkkhW...c",
    ".hHHHkkekW...c",
    ".hHHHHkkhW...I",
    ".hHHHHHHhW..IYI",
    "hHHHHHHHHW..iIi",
    "hHHHHHHHsW....",
    "hHHHHHHHHW....",
    "hHHHbbbbhW....",
    "hHHHHHHHHW....",
    "hHHHHHHHHW....",
    "hHHHhHHHHW....",
    "hHHHHHhHHW....",
    "hHHHHHHHHHW...",
    ".hHHhHHHHhW...",
    ".hhhhhhhh.W...",
    "...ss.ss......",
};

// Boss: the plague bishop, floating, crozier in hand.
const std::vector<std::string> BISHOP_TOP = {
    ".........O.......OOO..",
    "........OOO.....O...O.",
    ".......oRORo....O.Y.O.",
    ".......oRORo.....O..O.",
    "......oRRORRo.......O.",
    "......oOOOOOo.......O.",
    "......oRRORRo.......O.",
    ".....oRRRORRRo......O.",
    ".....oRRRORRRo......O.",
    ".....OOOOOOOOO......O.",
    ".....oOYOoOYOo......O.",
    "......sSSSSSs.......O.",
    "......SkkSkkS.......O.",
    "......SkeSkeS.......O.",
    "......sSSkSSs.......O.",
    ".......StttS........O.",
    "....rrRsSSsRRrr.....O.",
    "...rRRROOOOORRRRrrhhO.",
    "..rRRRRROORRRRRRRrhhO.",
    "..rRRRRROORRRRRr....O.",
    "..rRRRRROORRRRRr....O.",
    "..rRRROOOOOORRRr....O.",
    "..rRRRRROORRRRRr....O.",
    ".rRRRRRROORRRRRRr...O.",
    ".rRRRRRROORRRRRRr...O.",
    ".oOOOOOOOOOOOOOOo...O.",
};
const std::vector<std::string> BISHOP_HEM_A = {
    ".wWWWwWWWWWWwWWWw...O.",
    ".wWWWWwWWWWWWWwWw...o.",
    "..wWW.wWWWWw.WWw....o.",
    "..wW...wWWw..wWw....o.",
    "..w.....wWw...w.......",
    ".........w....w.......",
    ".........w............",
    "......................",
};
const std::vector<std::string> BISHOP_HEM_B = {
    ".wWWWwWWWWWWwWWWw...O.",
    ".wwWWWwWWWWWWWwWw...o.",
    ".wWW.wWWWWWWw.wWWw..o.",
    ".wW...wWWWWw...wW...o.",
    "..w....wWWw.....w.....",
    ".......wWw............",
    "........w.............",
    "......................",
};

// Boss: the pale horseman, galloping.
const std::vector<std::string> HORSE_A = {
    ".............IIIIIIIIi............",
    "...........IIiiiii................",
    "..........Ii......................",
    "..........i.......................",
    "..............cCCCCcW.............",
    "..............cCSSSsW...bBb.......",
    "..............cCkeSkW.YbBBBBb.....",
    "..............cCSStW...BkeBBBBb...",
    ".............cCCCstW..bbBBBBBBBBb.",
    "............c.cCCCCWhhb.bbBkBBBBBb",
    ".............cCCCCCWh.B...bbbbBtBB",
    "............c.cCRRCW..B.......bttt",
    "...Y.bBBb.....cCRRW...B...........",
    "..YYbBBBBBbBBBBBBBWBBBbb..........",
    ".YY.BBkkBBbkBkBkBkWbBBB...........",
    "YY..BBBBBb.kBkBkBkBbBBb...........",
    "Y...bBBbBb.kBkBkBkBkB.............",
    "Y......Bb...BkBkBkBbBb............",
    "......BBb...bbbbbbBb.Bb...........",
    "......BbBb.........Bb.Bb..........",
    ".....Bb.Bb.........Bb..Bb.........",
    "....Bb...Bb........Bb...Bb........",
    "....Bb...Bb.........Bb...BBb......",
    "...Bb...Bb..........Bb.....Bb.....",
    "...Bb...Bb...........Bb.....Bb....",
    "..Bb....Bb...........Bb......Bb...",
    "..kk....kk...........kk......kk...",
};
const std::vector<std::string> HORSE_B = {
    ".............IIIIIIIIi............",
    "...........IIiiiii................",
    "..........Ii......................",
    "..........i.......................",
    "..............cCCCCcW.............",
    "..............cCSSSsW...bBb.......",
    "..............cCkeSkW.YbBBBBb.....",
    "..............cCSStW...BkeBBBBb...",
    ".............cCCCstW..bbBBBBBBBBb.",
    "............c.cCCCCWhhb.bbBkBBBBBb",
    ".............cCCCCCWh.B...bbbbBtBB",
    "............c.cCRRCW..B.......bttt",
    "...Y.bBBb.....cCRRW...B...........",
    "..YYbBBBBBbBBBBBBBWBBBbb..........",
    ".YY.BBkkBBbkBkBkBkWbBBB...........",
    "YY..BBBBBb.kBkBkBkBbBBb...........",
    "Y...bBBbBb.kBkBkBkBkB.............",
    "Y.....Bb.Bb.BkBkBkBbBb............",
    ".....Bb..Bb.bbbbbbbBbBb...........",
    ".....Bb...Bb........BbBBb.........",
    "....Bb....Bb.........Bb.Bb........",
    "....Bb.....Bb.........Bbb.........",
    "...Bb......Bb..........BBb........",
    "..Bb........Bb........Bb.Bb.......",
    "..Bb........Bb.........Bb.Bb......",
    ".Bb..........Bb........Bb..Bb.....",
    ".kk..........kk........kk..kk.....",
};
const std::vector<std::string> HORSE_REAR = {
    "........IIIIIIIiW...bBb.......",
    "......IIiiiii...W..bBBBBb.....",
    ".....Ii.........W..BkeBBBBb...",
    ".....i.............bBBBBBBBBb.",
    "...............W....bbBkBBBBBb",
    ".........cCCc..W......bbbbBtBB",
    "........cCCCCc.W..Y.......bttt",
    "........cCSSSs.W..YbBB........",
    "........cCkeSk.W.YYbBB........",
    "........cCSStSW..YbBB.........",
    ".......cCCCstsW.YYbBB.........",
    "......c.cCCCCCWhYbBB..........",
    ".......cCCCCChW.YbBB..........",
    "......c.cCRRCCW..B.B..........",
    "........cCRRC.W.B.B...........",
    ".............W.BkB.....BBBb...",
    ".............W.BBk...BBb.Bb...",
    ".............WBkB.kk.b....Bb..",
    ".............BBBkBBkBb....Bb..",
    "............BkBBkk.BbBBb..Bb..",
    "...........BBBkkB.kb...Bb.kk..",
    "..........B.BBB.kBb....Bb.....",
    "....bBBb..BBkk.Bkb.....Bb.....",
    "..YYBBBBbBkBB.kB.b.....Bb.....",
    ".Y..BkkBb.B.kBB.b......Bb.....",
    "Y...bBBb.B..kk.b.......kk.....",
    "Y....Bb.B.....b...............",
    "....Bb..Bb...b................",
    "....Bb...Bb..b................",
    "...Bb.....Bbb.................",
    "....Bb.....Bb.................",
    "....Bb....Bb..................",
    ".....Bb...Bb..................",
    ".....kk...kk..................",
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

// ============================================================ Backdrop helpers

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

struct Stop { float y; uint32_t c; };

uint32_t gradient(const std::vector<Stop>& st, float y) {
    if (y <= st.front().y) return st.front().c;
    for (size_t i = 0; i + 1 < st.size(); i++)
        if (y <= st[i + 1].y) return mixRGB(st[i].c, st[i + 1].c, (y - st[i].y) / (st[i + 1].y - st[i].y));
    return st.back().c;
}

// Quantise to q levels per channel with 4x4 ordered dithering.
uint32_t dither(const float c[3], int x, int y, float q = 18) {
    uint32_t out = 0;
    for (int k = 0; k < 3; k++) {
        float v = c[k] / 255.f * q, b = std::floor(v);
        v = (b + ((v - b) > bayer(x, y) ? 1 : 0)) / q * 255.f;
        out |= uint32_t(clampf(v, 0, 255)) << (16 - k * 8);
    }
    return out;
}

void toF(uint32_t c, float o[3]) {
    o[0] = float((c >> 16) & 255);
    o[1] = float((c >> 8) & 255);
    o[2] = float(c & 255);
}
void towards(float c[3], uint32_t to, float t) {
    float o[3];
    toF(to, o);
    for (int k = 0; k < 3; k++) c[k] += (o[k] - c[k]) * t;
}

// Shaded, dithered moon with craters (the Plague Town moon generalised).
void drawMoon(Image& sky, float mx, float my, float mr, uint32_t lit, uint32_t dark, uint32_t rim, uint32_t cr1, uint32_t cr2) {
    for (int y = int(my - mr - 1); y <= int(my + mr + 1); y++)
        for (int x = int(mx - mr - 1); x <= int(mx + mr + 1); x++) {
            float dx = x + .5f - mx, dy = y + .5f - my;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d > mr) continue;
            float shade = (dx + dy) / (mr * 1.6f) + (d / mr) * 0.35f;
            uint32_t c = shade > bayer(x, y) + 0.25f ? dark : lit;
            if (d > mr - 1) c = rim;
            sky.set(x, y, argb(c));
        }
    struct Crater { float x, y, r; };
    const Crater cr[] = {{-5, -3, 3}, {4, 3, 2.5f}, {-1, 6, 2}, {3, -6, 1.6f}, {-7, 4, 1.4f}, {7, -2, 1.2f}};
    float k = mr / 14.f;
    for (auto& c : cr) {
        sky.disc(mx + c.x * k, my + c.y * k, c.r * k, argb(cr1));
        sky.disc(mx + c.x * k + 0.6f, my + c.y * k + 0.6f, c.r * k * 0.6f, argb(cr2));
    }
}

void scatterStars(Image& sky, StageArt& st, int n, int maxY, const std::function<bool(int, int)>& skip, const uint32_t cols[3], float amin,
                  float amax) {
    for (int i = 0; i < n; i++) {
        int x = irnd(0, VW - 1), y = irnd(0, maxY);
        if (skip(x, y)) continue;
        uint32_t col = cols[i % 3];
        uint32_t a = uint32_t(rnd(amin, amax));
        sky.blend(x, y, argb(col, a));
        if (i % 9 == 0) {
            sky.blend(x - 1, y, argb(col, a / 3));
            sky.blend(x + 1, y, argb(col, a / 3));
            sky.blend(x, y - 1, argb(col, a / 3));
            sky.blend(x, y + 1, argb(col, a / 3));
        }
        if (i % 4 == 0) st.stars.push_back({float(x), float(y), col, rnd(0, 6.28f)});
    }
}

// Paints a snow line on every upward-facing edge of a silhouette layer.
void snowcap(Image& l, uint32_t snow, uint32_t snow2, int depth = 2) {
    Image src = l;
    for (int x = 0; x < l.w; x++)
        for (int y = 0; y < l.h; y++) {
            if (!(src.get(x, y) >> 24)) continue;
            if (y > 0 && (src.get(x, y - 1) >> 24)) continue;
            l.set(x, y, argb(snow));
            for (int d = 1; d < depth; d++)
                if ((src.get(x, y + d) >> 24) && bayer(x, y + d) < 0.75f - d * 0.25f) l.set(x, y + d, argb(snow2));
        }
}

Image makeLampImage() {
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
    return outlined(l, OUTLINE);
}

// Bakes a street lamp (feet at y=166) into the town and ground layers and registers its glass and light.
constexpr int LAMP_OFS = 5;
void bakeLamp(Image& town, Image& ground, StageArt& st, int lx, uint32_t glass, uint32_t light, uint32_t halo, float phase) {
    static const Image lamp = makeLampImage();
    int x0 = lx - LAMP_OFS, y0 = 166 - lamp.h;
    town.blit(lamp, x0, y0);
    ground.blit(lamp, x0, y0 - 158);
    st.windows.push_back({float(x0 + 3), float(y0 + 3), 1, 6, glass, phase});
    st.windows.push_back({float(x0 + 5), float(y0 + 3), 1, 6, glass, phase + 0.4f});
    st.lights.push_back({float(lx), 128, 74, light, 0.9f, 0.15f});
    st.glows.push_back({float(x0 + 4.5f), float(y0 + 6), 16, halo, 0.55f, 0.15f});
}

void deadTree(Image& img, float x, float y, float ang, float len, int depth, uint32_t c) {
    float x2 = x + std::cos(ang) * len, y2 = y + std::sin(ang) * len;
    img.line(int(x), int(y), int(x2), int(y2), c);
    if (depth > 3) img.line(int(x) + 1, int(y), int(x2) + 1, int(y2), c);
    if (depth <= 0) return;
    int kids = irnd(2, 3);
    for (int i = 0; i < kids; i++) deadTree(img, x2, y2, ang + rnd(-0.75f, 0.75f), len * rnd(0.55f, 0.75f), depth - 1, c);
}

// Perspective cobbles (the Plague Town street) over rows [y0, GH) restricted to x in [xa, xb).
void cobbles(Image& g, int y0, int xa, int xb, const uint32_t* stones, int ns, uint32_t hi, uint32_t moss, float mossP) {
    int y = y0, row = 0;
    while (y < g.h) {
        int rh = 3 + row / 2;
        int x = xa - irnd(0, rh * 2);
        while (x < xb) {
            int sw = rh * 2 + irnd(-1, 2);
            uint32_t c = stones[irnd(0, ns - 1)];
            g.fill(x + 1, y + 1, sw - 1, rh - 1, argb(c));
            g.fill(x + 1, y + 1, sw - 2, 1, argb(mixRGB(c, hi, 0.35f)));
            g.fill(x + 1, y + rh - 1, sw - 1, 1, argb(mixRGB(c, 0x000000, 0.35f)));
            if (chance(mossP)) g.set(x + irnd(2, sw - 2), y + irnd(1, rh - 1), argb(moss));
            x += sw;
        }
        y += rh;
        row++;
    }
}

// ============================================================ Stage 0: Plague Town

void buildSky0(StageArt& st) {
    Image sky(VW, VH);
    const Stop stops[] = {{0, 0x0a0918}, {50, 0x15132e}, {95, 0x2a1f45}, {125, 0x43305a}, {145, 0x5a4560}, {162, 0x5e5a58}, {216, 0x5e5a58}};
    const float mx = 62, my = 46, mr = 14;
    for (int y = 0; y < VH; y++) {
        int s = 0;
        while (s < 5 && y > stops[s + 1].y) s++;
        float t = (y - stops[s].y) / (stops[s + 1].y - stops[s].y);
        uint32_t base = mixRGB(stops[s].c, stops[s + 1].c, t);
        for (int x = 0; x < VW; x++) {
            float c[3];
            toF(base, c);
            float d = std::hypot(x - mx, y - my);
            if (d > mr) {
                float h = std::pow(std::max(0.f, 1 - (d - mr) / 60.f), 2.2f) * 0.45f;
                c[0] += (200 - c[0]) * h;
                c[1] += (205 - c[1]) * h;
                c[2] += (230 - c[2]) * h;
            }
            sky.set(x, y, argb(dither(c, x, y)));
        }
    }
    const uint32_t cols[3] = {0xd0d8ff, 0xfff0c0, 0xffffff};
    scatterStars(sky, st, 110, 120, [&](int x, int y) { return std::hypot(x - mx, y - my) < 34; }, cols, 70, 220);
    drawMoon(sky, mx, my, mr, 0xece8cc, 0xbdb89a, 0xd8d4b8, 0xc4bf9f, 0xb0ab8c);
    st.sky = gfx::texture(sky);
    st.lights.push_back({mx, my, 170, 0x5a5a90, 0.7f, 0});
}

Image buildTown0(StageArt& st) {
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
            st.windows.push_back({float(tx + 5), float(base - th + 10), 4, 4, 0xff6a5a, 0});
            t.fill(tx + 5, base - th + 24, 4, 10, argb(0x3a2a40));
            st.windows.push_back({float(tx + 5), float(base - th + 24), 2, 10, 0x6ab0ff, 1});
            st.windows.push_back({float(tx + 7), float(base - th + 24), 2, 10, 0xffd05a, 2});
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
                    st.windows.push_back({float(wx), float(wy), 2, 3, chance(0.7f) ? 0xffc45au : 0xff9a40u, rnd(0, 6.28f)});
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
                st.windows.push_back({float(wx), float(wy), 5, 6, 0xffb048, rnd(0, 6.28f)});
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
    return t;
}

Image buildGround0() {
    const int GH = VH - 158;
    Image g(VW, GH, argb(0x1c1b24));
    // Curb
    g.fill(0, 0, VW, 4, argb(0x34303e));
    g.fill(0, 0, VW, 1, argb(0x4a4458));
    g.fill(0, 3, VW, 1, argb(0x16141c));
    for (int x = 0; x < VW; x += irnd(8, 14)) g.set(x, 1, argb(0x22202a)), g.set(x, 2, argb(0x22202a));
    const uint32_t stones[] = {0x2e2d39, 0x34323f, 0x2a2934, 0x383444, 0x302c38};
    cobbles(g, 4, 0, VW, stones, 5, 0x6a6480, 0x24331f, 0.15f);
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
    return g;
}

// ============================================================ Stage 1: Blighted Marsh

void buildMarsh(StageArt& st) {
    rngState = 0x5EB09u;
    // ---- Sky: sickly teal night with a veiled green moon and drifting haze.
    Image sky(VW, VH);
    const std::vector<Stop> stops = {{0, 0x050a0d}, {50, 0x0a1618}, {95, 0x142a28}, {125, 0x224038}, {145, 0x38584a}, {162, 0x4c6a58}, {216, 0x4c6a58}};
    const float mx = 292, my = 40, mr = 11;
    for (int y = 0; y < VH; y++) {
        uint32_t base = gradient(stops, float(y));
        for (int x = 0; x < VW; x++) {
            float c[3];
            toF(base, c);
            float d = std::hypot(x - mx, y - my);
            if (d > mr) {
                float h = std::pow(std::max(0.f, 1 - (d - mr) / 70.f), 2.0f) * 0.42f;
                towards(c, 0xb0d0a0, h);
            }
            // Horizontal haze bands
            float n = vnoise(x / 28.f, y / 6.f, 64, 11);
            float haze = clampf((n - 0.55f) * 2.2f, 0, 1) * smooth((y - 30) / 90.f) * 0.18f;
            towards(c, 0x6a907a, haze);
            sky.set(x, y, argb(dither(c, x, y)));
        }
    }
    const uint32_t cols[3] = {0xc0ffd8, 0xe0f0c0, 0xa0d0c8};
    scatterStars(sky, st, 70, 105, [&](int x, int y) { return std::hypot(x - mx, y - my) < 28; }, cols, 50, 160);
    drawMoon(sky, mx, my, mr, 0xd8e0b0, 0xa8b484, 0xc0cc9c, 0xb0bc8c, 0x98a478);
    st.sky = gfx::texture(sky);
    st.lights.push_back({mx, my, 150, 0x4a6a58, 0.65f, 0});

    // ---- Town layer
    Image t(VW, VH);
    // Far tree line
    const uint32_t FC = argb(0x11201f);
    for (int x = 0; x < VW; x++) {
        float n = vnoise(x / 9.f, 0.5f, 64, 3) * 0.6f + vnoise(x / 3.f, 3.5f, 256, 4) * 0.4f;
        int h = int(10 + n * 16);
        t.fill(x, 160 - h, 1, h, FC);
    }
    for (int i = 0; i < 9; i++) deadTree(t, rnd(0, VW), 150, -PI / 2 + rnd(-0.2f, 0.2f), rnd(10, 16), 3, FC);

    // Sunken chapel ruin, leaning into the bog (drawn upright, then sheared).
    {
        const uint32_t RC = argb(0x0f1d1c), RD = argb(0x0a1414);
        Image r(70, 90);
        // Nave wall with a broken gable
        r.fill(4, 34, 40, 56, RC);
        for (int i = 0; i < 20; i++) r.fill(4 + i, 34 - i, 40 - i * 2, 1, RC);
        for (int y = 10; y < 46; y++)
            for (int x = 22; x < 46; x++)
                if ((x - 22) * 1.1f + vnoise(x / 2.f, y / 2.f, 64, 13) * 6 > (y - 10) * 0.9f + 4) r.set(x, y, 0); // collapsed roof
        // Bell tower, broken top
        r.fill(46, 8, 16, 82, RC);
        for (int x = 46; x < 62; x++) r.fill(x, 8 - (x * 7 % 5), 1, x * 7 % 5, RC);
        r.fill(44, 22, 20, 2, RC);
        // Gothic window holes (the sky shows through)
        auto arch = [&](int ax, int ay, int w, int h) {
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    float cx = (x + .5f) - w / 2.f;
                    if (y < w / 2 && cx * cx + (y - w / 2.f) * (y - w / 2.f) * 1.6f > (w / 2.f) * (w / 2.f)) continue;
                    r.set(ax + x, ay + y, 0);
                }
        };
        arch(9, 40, 6, 16);
        arch(21, 46, 6, 14);
        arch(33, 50, 6, 12);
        arch(51, 26, 6, 12);
        r.fill(52, 32, 4, 1, RD);
        // Toppled cross on the gable
        r.fill(10, 6, 1, 9, RC);
        r.line(7, 11, 13, 9, RC);
        // Shear (leaning right) and sink into the ground
        for (int y = 0; y < r.h; y++)
            for (int x = 0; x < r.w; x++) {
                uint32_t c = r.get(x, y);
                if (!(c >> 24)) continue;
                int sx = x + int((90 - y) * 0.16f);
                t.set(132 + sx, 74 + y, c);
            }
        // A lone candle still burns in the tower
        int cx = 132 + 53 + int((90 - 36) * 0.16f), cy = 74 + 34;
        t.fill(cx, cy, 2, 3, argb(0x3a3020));
        st.windows.push_back({float(cx), float(cy), 2, 3, 0x9aff8a, 1.3f});
        st.lights.push_back({float(cx + 1), float(cy + 1), 30, 0x7aff8a, 0.4f, 0.3f});
    }

    // Mid layer: drowned trees with hanging moss, a gibbet and a fisher's hut on stilts.
    const uint32_t MC = argb(0x08120f);
    Image m(VW, VH);
    deadTree(m, 34, 166, -PI / 2 - 0.15f, 26, 5, MC);
    deadTree(m, 120, 166, -PI / 2 + 0.2f, 16, 4, MC);
    deadTree(m, 222, 166, -PI / 2 - 0.05f, 22, 5, MC);
    // Hanging moss strands under branches
    for (int i = 0; i < 260; i++) {
        int x = irnd(0, 300), y = irnd(60, 130);
        if (!(m.get(x, y) >> 24) || (m.get(x, y + 1) >> 24)) continue;
        int len = irnd(3, 11);
        for (int k = 1; k <= len; k++)
            if (k < len - 2 || bayer(x, y + k) < 0.5f) m.set(x + (k > 6 && (x & 1) ? 1 : 0), y + k, argb(0x0c1a14));
    }
    // Gibbet cage hanging from the big tree
    {
        int gx = 52, gy = 118;
        m.line(gx, 98, gx, gy, MC);
        for (int y = 0; y < 12; y++) {
            m.set(gx - 4, gy + y, MC);
            m.set(gx + 4, gy + y, MC);
            if (y % 4 == 0) m.fill(gx - 4, gy + y, 9, 1, MC);
        }
        m.set(gx - 2, gy + 2, MC), m.set(gx, gy + 1, MC), m.set(gx + 2, gy + 2, MC);
        m.fill(gx - 3, gy + 12, 7, 1, MC);
        m.fill(gx - 1, gy + 4, 3, 6, MC); // poor soul
    }
    // Hut on stilts
    {
        int hx = 244, base = 162;
        for (int sx : {hx + 1, hx + 9, hx + 19, hx + 27}) m.fill(sx, base - 14, 1, 14, MC);
        m.line(hx + 1, base - 2, hx + 9, base - 12, MC);
        m.line(hx + 19, base - 12, hx + 27, base - 2, MC);
        m.fill(hx - 1, base - 15, 31, 2, MC);
        m.fill(hx + 2, base - 33, 25, 18, MC);
        for (int i = 0; i < 18; i++) m.fill(hx - 3 + i, base - 33 - i * 2 / 3, 35 - i * 2, 1, MC);
        for (int i = 0; i < 6; i++) m.set(hx - 3 + irnd(0, 34), base - 33 + 1, MC); // ragged thatch
        m.fill(hx + 22, base - 50, 3, 8, MC);
        m.fill(hx + 6, base - 28, 5, 6, argb(0x2e2a1e));
        st.windows.push_back({float(hx + 6), float(base - 28), 5, 6, 0xffa848, 0.7f});
        st.lights.push_back({float(hx + 8), float(base - 25), 34, 0xffa048, 0.5f, 0.2f});
        // Lantern on a pole by the jetty
        m.fill(hx - 8, base - 30, 1, 30, MC);
        m.fill(hx - 8, base - 30, 5, 1, MC);
        m.fill(hx - 5, base - 29, 1, 2, MC);
        m.fill(hx - 6, base - 27, 3, 3, argb(0x2a2a1a));
        st.windows.push_back({float(hx - 6), float(base - 27), 3, 3, 0xd0ff8a, 2.1f});
        st.glows.push_back({float(hx - 4.5f), float(base - 25.5f), 10, 0xa0ff7a, 0.6f, 0.25f});
        st.lights.push_back({float(hx - 4.5f), float(base - 25), 46, 0x7ac060, 0.55f, 0.2f});
    }
    // Reeds and cattails along the bank
    for (int i = 0; i < 150; i++) {
        int x = irnd(0, 300), h = irnd(5, 15);
        uint32_t c = chance(0.5f) ? MC : argb(0x10201a);
        int lean = irnd(-1, 1);
        for (int k = 0; k < h; k++) m.set(x + (k > h / 2 ? lean : 0), 159 - k, c);
        if (chance(0.3f)) m.fill(x + lean, 159 - h - 1, 1, 3, argb(0x1a1410));
    }
    t.blit(m, 0, 0);
    // Low mist hanging over the water
    for (int y = 126; y < 160; y++)
        for (int x = 0; x < VW; x++) {
            float n = vnoise(x / 20.f, y / 5.f, 64, 21);
            float a = clampf((n - 0.4f) * 2.f, 0, 1) * smooth((y - 126) / 26.f) * 0.55f;
            if (a > bayer(x, y)) t.blend(x, y, argb(0x5a8070, 70));
        }

    // Will-o'-wisps
    const float wisp[][3] = {{78, 140, 0}, {136, 128, 1}, {190, 146, 0}, {16, 120, 1}, {275, 136, 0}};
    for (auto& w : wisp) {
        uint32_t col = w[2] > 0 ? 0x60e8ff : 0x7aff9a;
        st.glows.push_back({w[0], w[1], 13, col, 0.45f, 0.6f});
        st.glows.push_back({w[0], w[1], 3.5f, 0xe8fff0, 0.9f, 0.5f});
        st.lights.push_back({w[0], w[1], 44, col, 0.45f, 0.6f});
        t.set(int(w[0]), int(w[1]), argb(0xd0fff0));
    }

    // ---- Ground: soggy earth, black pools, a boardwalk and firm stones near the clinic.
    const int GH = VH - 158;
    Image g(VW, GH);
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < VW; x++) {
            float z = 60.f / (y + 10.f); // perspective depth: features flatten toward the horizon
            float n = vnoise(x / 11.f, z * 2.2f, 256, 31) * 0.7f + vnoise(x / 4.f, z * 6.f, 256, 32) * 0.3f;
            float c[3];
            toF(mixRGB(0x262c20, 0x2e3424, y / float(GH)), c);
            if (n > 0.58f) towards(c, 0x34402a, (n - 0.58f) * 2.5f);
            if (n < 0.38f) towards(c, 0x12160f, (0.38f - n) * 2.5f);
            g.set(x, y, argb(dither(c, x, y, 24)));
        }
    // Bank edge
    g.fill(0, 0, VW, 3, argb(0x232a1e));
    g.fill(0, 0, VW, 1, argb(0x34402a));
    g.fill(0, 3, VW, 1, argb(0x101410));
    for (int x = 0; x < VW; x += irnd(2, 5)) g.set(x, 0, argb(chance(0.5f) ? 0x3e4c30 : 0x2a3420));
    // Murky pools with green reflections
    for (int i = 0; i < 6; i++) {
        float px = rnd(14, 280), py = rnd(12, GH - 6), rx = rnd(10, 22), ry = rx * 0.22f;
        for (int yy = int(py - ry); yy <= int(py + ry); yy++)
            for (int xx = int(px - rx); xx <= int(px + rx); xx++) {
                float dx = (xx - px) / rx, dy = (yy - py) / ry;
                float d = dx * dx + dy * dy;
                if (d > 1) continue;
                uint32_t c = d > 0.75f ? 0x16261f : (dy < -0.2f ? 0x203a36 : 0x142a28);
                g.set(xx, yy, argb(c));
            }
        g.fill(int(px - rx * 0.5f), int(py), int(rx * 0.4f), 1, argb(0x5a8a70));
        if (chance(0.6f)) g.fill(int(px + rx * 0.2f), int(py + 1), 2, 1, argb(0x2e5a3a)); // lily pad
    }
    // Sparse grass tufts and bones
    for (int i = 0; i < 90; i++) {
        int x = irnd(0, 300), y = irnd(4, GH - 1);
        g.set(x, y, argb(0x2e3a24));
        g.set(x + 1, y - 1, argb(0x3a4a2c));
    }
    for (int i = 0; i < 5; i++) {
        int x = irnd(10, 280), y = irnd(10, GH - 4);
        g.fill(x, y, 4, 1, argb(0x8a8670));
        g.set(x - 1, y - 1, argb(0x8a8670));
        g.set(x + 4, y + 1, argb(0x8a8670));
    }
    // Half-sunk planks and a rotten stump
    for (int i = 0; i < 3; i++) {
        int px = irnd(30, 260), py = irnd(10, GH - 8), len = irnd(10, 16);
        g.fill(px, py, len, 2, argb(0x3a2c1e));
        g.fill(px, py, len, 1, argb(0x4e3c28));
        g.fill(px + 1, py + 2, len - 2, 1, argb(0x0e100c));
        g.set(px + len / 3, py, argb(0x22180e));
    }
    // Firm stones leading to the clinic
    const uint32_t stones[] = {0x2c2e2a, 0x34362e, 0x282a26, 0x303428};
    Image path(VW, GH);
    cobbles(path, 4, 290, VW, stones, 4, 0x6a7460, 0x24331f, 0.25f);
    for (int y = 0; y < GH; y++)
        for (int x = 280; x < VW; x++) {
            float edge = (x - 286) / 18.f + vnoise(x / 3.f, y / 3.f, 128, 9) * 0.6f - 0.3f;
            if (edge > bayer(x, y) && (path.get(x, y) >> 24)) g.set(x, y, path.get(x, y));
        }

    st.town = gfx::texture(t);
    st.ground = gfx::texture(g);
}

// ============================================================ Stage 2: Burning Cathedral

void flame(Image& img, int cx, int base, int w, int h, uint32_t seed) {
    for (int y = 0; y < h; y++)
        for (int x = -w / 2; x <= w / 2; x++) {
            float u = float(y) / h;                          // 0 at base, 1 at top
            float width = (w / 2.f) * (1 - u * u) + 0.5f;     // taper
            float n = vnoise((cx + x) / 2.5f, (base - y) / 3.f, 256, seed);
            if (std::fabs(float(x)) > width * (0.6f + n * 0.7f)) continue;
            if (u > 0.55f && n < 0.45f) continue;
            float core = 1 - std::fabs(float(x)) / (width + 0.01f);
            uint32_t c = u < 0.35f && core > 0.4f ? 0xffe090 : (u < 0.65f && core > 0.2f ? 0xffa030 : 0xd8501a);
            img.set(cx + x, base - y, argb(c));
        }
}

void buildCathedral(StageArt& st) {
    rngState = 0xF1AE5u;
    // ---- Sky: smoke-choked, lit from below by the burning city. No stars.
    Image sky(VW, VH);
    const std::vector<Stop> stops = {{0, 0x0d0508}, {40, 0x1c080b}, {85, 0x36110f}, {120, 0x5a1d12}, {145, 0x823318}, {162, 0x9a4520}, {216, 0x9a4520}};
    const float mx = 300, my = 34, mr = 9;
    for (int y = 0; y < VH; y++) {
        uint32_t base = gradient(stops, float(y));
        for (int x = 0; x < VW; x++) {
            float c[3];
            toF(base, c);
            float d = std::hypot(x - mx, y - my);
            if (d < mr) towards(c, 0xb04a30, 0.85f);
            else towards(c, 0xc05030, std::pow(std::max(0.f, 1 - (d - mr) / 40.f), 2.f) * 0.3f);
            // Glow of the fires along the horizon, strongest behind the cathedral
            float hg = smooth((y - 80) / 80.f) * (0.5f + 0.5f * std::exp(-std::pow((x - 180) / 90.f, 2)));
            towards(c, 0xd8702a, hg * 0.35f);
            // Rolling smoke
            float n = vnoise(x / 34.f + y / 60.f, y / 14.f, 64, 5) * 0.65f + vnoise(x / 12.f, y / 6.f, 128, 6) * 0.35f;
            float s = clampf((n - 0.42f) * 2.6f, 0, 1) * (1 - smooth((y - 100) / 70.f)) * 0.85f;
            uint32_t smokeC = mixRGB(0x140c0e, 0x5a2a1c, smooth((y - 40) / 110.f));
            towards(c, smokeC, s);
            sky.set(x, y, argb(dither(c, x, y)));
        }
    }
    // Floating embers
    for (int i = 0; i < 70; i++) {
        int x = irnd(0, VW - 1), y = irnd(30, 150);
        sky.blend(x, y, argb(chance(0.5f) ? 0xffb050 : 0xff7020, uint32_t(rnd(80, 200))));
    }
    st.sky = gfx::texture(sky);
    st.lights.push_back({192, 150, 240, 0x6a2410, 0.5f, 0.08f}); // the whole city glows

    // ---- Town layer
    Image t(VW, VH);
    // Far burning city
    const uint32_t FC = argb(0x240c10);
    int x = -4;
    std::vector<std::array<int, 3>> fires; // x, y, size
    while (x < VW) {
        int w = irnd(14, 28), h = irnd(14, 36), base = 160;
        t.fill(x, base - h, w, h, FC);
        int roof = irnd(0, 2);
        if (roof == 0)
            for (int i = 0; i < w / 2 + 1; i++) t.fill(x + i, base - h - i, w - i * 2, 1, FC);
        else if (roof == 1)
            t.fill(x + 2, base - h - irnd(5, 9), 3, 9, FC);
        else
            for (int i = 0; i < w; i++) t.fill(x + i, base - h - i / 3, 1, i / 3 + 1, FC);
        for (int wy = base - h + 4; wy < base - 6; wy += 7)
            for (int wx = x + 3; wx < x + w - 3; wx += 6)
                if (chance(0.22f)) {
                    t.fill(wx, wy, 2, 3, argb(0x3a1a14));
                    st.windows.push_back({float(wx), float(wy), 2, 3, chance(0.6f) ? 0xff7a28u : 0xff4a18u, rnd(0, 6.28f)});
                }
        if (chance(0.35f) && (x < 100 || x > 270)) fires.push_back({x + w / 2, base - h - (roof == 0 ? w / 2 : 0), irnd(5, 9)});
        x += w + irnd(-3, 2);
    }
    // Smoke columns behind the cathedral
    for (int y = 10; y < 150; y++)
        for (int xx = 0; xx < VW; xx++) {
            float cxs[] = {70, 160, 300};
            for (float cx : cxs) {
                float wdt = 6 + (150 - y) * 0.25f;
                float off = std::sin(y * 0.05f + cx) * 8 + (150 - y) * 0.2f;
                float dx = std::fabs(xx - cx - off) / wdt;
                float n = vnoise(xx / 8.f, y / 8.f, 128, 15);
                if (dx < 1 && n * (1 - dx) > 0.22f + bayer(xx, y) * 0.3f && !(t.get(xx, y) >> 24)) t.blend(xx, y, argb(0x1a1012, 150));
            }
        }

    // The cathedral
    const uint32_t CC = argb(0x170a0d), CD = argb(0x0f0609);
    {
        int b = 160;
        // Nave and roof
        t.fill(140, 104, 84, b - 104, CC);
        for (int i = 0; i < 16; i++) t.fill(140 + i, 104 - i, 84 - i * 2, 1, CC);
        // Flying buttresses
        for (int k = 0; k < 4; k++) {
            int bx = 146 + k * 22;
            t.fill(bx, 120, 3, b - 120, CC);
            t.line(bx + 1, 120, bx + 8, 108, CC);
            t.line(bx + 1, 121, bx + 8, 109, CC);
            t.fill(bx, 117, 3, 3, CC);
            t.set(bx + 1, 116, CC);
        }
        // West towers with spires (the left one broken and burning)
        auto tower = [&](int tx, int top, bool broken) {
            t.fill(tx, top, 22, b - top, CC);
            t.fill(tx - 2, top, 26, 3, CC);
            for (int px : {tx - 1, tx + 22}) t.fill(px, top - 6, 2, 6, CC); // pinnacles
            if (!broken) {
                for (int i = 0; i < 42; i++) t.fill(tx + 1 + i / 4, top - i, 20 - (i / 4) * 2, 1, CC);
                t.fill(tx + 10, top - 50, 2, 8, CC);
                t.fill(tx + 8, top - 47, 6, 2, CC);
            } else {
                for (int i = 0; i < 16; i++) {
                    int w2 = 20 - (i / 4) * 2;
                    int cut = (i * 5) % 7;
                    t.fill(tx + 1 + i / 4 + cut / 2, top - i, std::max(1, w2 - cut), 1, CC);
                }
                fires.push_back({tx + 11, top - 6, 13});
            }
            // Lancet windows
            for (int k = 0; k < 2; k++) {
                int wx = tx + 5 + k * 9, wy = top + 14;
                t.fill(wx, wy - 1, 3, 15, CD);
                t.set(wx + 1, wy - 2, CD);
                st.windows.push_back({float(wx), float(wy), 3, 13, k ? 0xff5a20u : 0xffa040u, rnd(0, 6.28f)});
            }
            for (int k = 0; k < 2; k++) {
                int wx = tx + 6 + k * 8, wy = top + 40;
                t.fill(wx, wy, 2, 6, CD);
                st.windows.push_back({float(wx), float(wy), 2, 6, 0xff6a28, rnd(0, 6.28f)});
            }
        };
        tower(118, 64, true);
        tower(224, 56, false);
        // Crossing spire
        for (int i = 0; i < 40; i++) t.fill(178 + i / 8, 88 - i, 8 - (i / 8) * 2, 1, CC);
        t.fill(181, 40, 2, 10, CC);
        t.fill(179, 43, 6, 2, CC);
        // Great rose window, built from horizontal strips
        const int rx = 182, ry = 124, rr = 9;
        t.disc(rx, ry, rr + 1.5f, CD);
        for (int y = -rr; y < rr; y += 2) {
            float hw = std::sqrt(std::max(0.f, float(rr * rr) - (y + 1) * (y + 1)));
            int w2 = int(hw * 2);
            if (w2 < 2) continue;
            uint32_t col = (y / 2) % 2 ? 0xff5a28 : 0xffb040;
            int l = rx - w2 / 2, half = w2 / 2;
            if (half >= 2) {
                st.windows.push_back({float(l), float(ry + y), float(half - 1), 1.f, col, float(y)});
                st.windows.push_back({float(rx + 1), float(ry + y), float(w2 - half - 1), 1.f, col, float(y) + 0.5f});
            } else {
                st.windows.push_back({float(l), float(ry + y), float(w2), 1.f, col, float(y)});
            }
        }
        st.glows.push_back({float(rx), float(ry), 18, 0xff7a30, 0.3f, 0.3f});
        st.lights.push_back({float(rx), float(ry), 70, 0xff6a20, 0.75f, 0.3f});
        // Nave lancets
        for (int k = 0; k < 6; k++) {
            int wx = 146 + k * 13 + (k > 2 ? 10 : 0), wy = 128;
            if (k == 2 || k == 3) continue;
            t.fill(wx, wy - 1, 3, 14, CD);
            st.windows.push_back({float(wx), float(wy), 3, 12, k % 2 ? 0xff4a18u : 0xff8a30u, rnd(0, 6.28f)});
        }
        // Portal
        t.fill(176, 142, 12, 18, CD);
        for (int i = 0; i < 6; i++) t.fill(177 + i / 2, 141 - i, 10 - i, 1, CD);
        st.windows.push_back({178, 146, 8, 14, 0xff6a20, 0.3f});
        st.lights.push_back({182, 154, 50, 0xff7020, 0.6f, 0.35f});
        fires.push_back({160, 94, 7});
        fires.push_back({206, 94, 6});
    }

    // Mid layer: gutted houses with bare beams, burning inside.
    const uint32_t MC = argb(0x0c0507);
    auto ruin = [&](int hx, int w, int h, int base, bool left) {
        t.fill(hx, base - h, w, h, MC);
        // Roof frame: only the rafters remain
        int peak = w / 2 + 3;
        for (int i = 0; i <= peak; i += 3) {
            t.line(hx - 2 + i, base - h - i, hx - 2 + i, base - h, MC);
            t.line(hx + w + 1 - i, base - h - i, hx + w + 1 - i, base - h, MC);
        }
        t.line(hx - 2, base - h, hx - 2 + peak, base - h - peak, MC);
        t.line(hx + w + 1, base - h, hx + w + 1 - peak, base - h - peak, MC);
        // Broken half
        for (int i = 0; i < 9; i++) t.fill(left ? hx + w - 10 + i : hx + i, base - h, 1, irnd(2, 10), argb(0, 0));
        for (int wy = base - h + 6; wy < base - 8; wy += 12)
            for (int wx = hx + 4; wx < hx + w - 6; wx += 10) {
                if (!chance(0.6f)) continue;
                t.fill(wx, wy, 5, 6, argb(0x3a1a12));
                st.windows.push_back({float(wx), float(wy), 5, 6, chance(0.5f) ? 0xff8a30u : 0xff5a20u, rnd(0, 6.28f)});
            }
        fires.push_back({hx + w / 2 + (left ? -4 : 4), base - h - 2, irnd(8, 11)});
    };
    ruin(-6, 44, 50, 166, true);
    ruin(262, 34, 40, 166, false);
    // Charred gallows and a cart wreck
    t.fill(100, 124, 2, 42, MC);
    t.fill(100, 124, 16, 2, MC);
    t.line(102, 132, 108, 126, MC);
    t.fill(114, 126, 1, 8, MC);
    t.fill(112, 134, 5, 6, MC);
    t.disc(240, 156, 5, MC);
    t.disc(240, 156, 3, argb(0, 0));
    t.fill(239, 155, 3, 3, MC);
    t.line(222, 152, 246, 148, MC);
    t.line(222, 153, 246, 149, MC);
    t.fill(224, 146, 18, 3, MC);

    // Bake flames and register their lights
    for (auto& f : fires) {
        flame(t, f[0], f[1], f[2], f[2] * 2, uint32_t(f[0] * 7 + f[1]));
        st.glows.push_back({float(f[0]), float(f[1] - f[2]), f[2] * 2.6f, 0xff8a30, 0.55f, 0.5f});
        st.lights.push_back({float(f[0]), float(f[1] - f[2] / 2), f[2] * 9.f, 0xff6a20, 0.8f, 0.4f});
    }

    // ---- Ground: ash-grey cobbles, soot and embers
    const int GH = VH - 158;
    Image g(VW, GH, argb(0x1c1a1c));
    g.fill(0, 0, VW, 4, argb(0x3a3436));
    g.fill(0, 0, VW, 1, argb(0x5a4a44));
    g.fill(0, 3, VW, 1, argb(0x141214));
    for (int xx = 0; xx < VW; xx += irnd(8, 14)) g.set(xx, 1, argb(0x242022)), g.set(xx, 2, argb(0x242022));
    const uint32_t stones[] = {0x3a3738, 0x423e3e, 0x353233, 0x48433f, 0x3e3a3a};
    cobbles(g, 4, 0, VW, stones, 5, 0x8a7a70, 0x1a1414, 0.25f);
    // Soot stains
    for (int y = 0; y < GH; y++)
        for (int xx = 0; xx < 300; xx++) {
            float n = vnoise(xx / 18.f, y / 5.f, 64, 41);
            if (n > 0.66f && bayer(xx, y) < (n - 0.66f) * 3) g.set(xx, y, argb(0x242022));
        }
    // Scattered debris: charred beams and rubble
    for (int i = 0; i < 7; i++) {
        int bx = irnd(10, 280), by = irnd(8, GH - 6), len = irnd(8, 16);
        g.line(bx, by, bx + len, by + irnd(-2, 2), argb(0x241812));
        g.line(bx, by + 1, bx + len, by + 1 + irnd(-1, 1), argb(0x140c0a));
        g.set(bx + len, by, argb(0xff6a20));
    }
    for (int i = 0; i < 25; i++) {
        int rx2 = irnd(0, 295), ry2 = irnd(4, GH - 2);
        g.fill(rx2, ry2, 2, 1, argb(0x5a524c));
    }
    // Glowing embers on the street
    for (int i = 0; i < 18; i++) {
        float ex = float(irnd(4, 296)), ey = float(158 + irnd(6, GH - 3));
        st.windows.push_back({ex, ey, 1, 1, chance(0.5f) ? 0xffa040u : 0xff5a18u, rnd(0, 6.28f)});
    }
    st.town = gfx::texture(t);
    st.ground = gfx::texture(g);
}

// ============================================================ Stage 3: Frozen Abbey

void pine(Image& img, int x, int base, int h, uint32_t c) {
    img.fill(x, base - h / 5, 2, h / 5, c);
    int tiers = std::max(3, h / 8);
    for (int k = 0; k < tiers; k++) {
        int ty = base - h / 5 - k * (h * 4 / 5) / tiers;
        int tw = int((tiers - k + 1) * h * 0.11f);
        int th = (h * 4 / 5) / tiers + 3;
        for (int i = 0; i < th; i++) {
            int ww = tw * (i + 1) / th;
            img.fill(x + 1 - ww, ty - th + i + 1, ww * 2, 1, c);
        }
    }
    img.set(x, base - h - 1, c);
}

void buildAbbey(StageArt& st) {
    rngState = 0x1CEB0u;
    // ---- Sky: clear cold night, big pale moon, a faint aurora.
    Image sky(VW, VH);
    const std::vector<Stop> stops = {{0, 0x070d1c}, {50, 0x0e1c36}, {95, 0x1a2e52}, {125, 0x2c446a}, {145, 0x435e82}, {162, 0x5e7896}, {216, 0x5e7896}};
    const float mx = 74, my = 40, mr = 15;
    for (int y = 0; y < VH; y++) {
        uint32_t base = gradient(stops, float(y));
        for (int x = 0; x < VW; x++) {
            float c[3];
            toF(base, c);
            float d = std::hypot(x - mx, y - my);
            if (d > mr) towards(c, 0xd0e0ff, std::pow(std::max(0.f, 1 - (d - mr) / 70.f), 2.2f) * 0.5f);
            // Aurora ribbon
            float ay = 34 + std::sin(x / 41.f) * 10 + std::sin(x / 17.f + 1) * 4;
            float dy = (y - ay) / 12.f;
            if (dy > -1.5f && dy < 3.f) {
                float a = std::exp(-dy * dy * (dy < 0 ? 3.f : 0.6f)) * smooth((x - 120) / 80.f) * (0.6f + 0.4f * vnoise(x / 6.f, 0, 128, 51));
                towards(c, 0x5ad8b0, a * 0.22f);
            }
            sky.set(x, y, argb(dither(c, x, y)));
        }
    }
    const uint32_t cols[3] = {0xd0e0ff, 0xffffff, 0xb0c8ff};
    scatterStars(sky, st, 150, 115, [&](int x, int y) { return std::hypot(x - mx, y - my) < 36; }, cols, 80, 230);
    drawMoon(sky, mx, my, mr, 0xf0f6ff, 0xc0ccdc, 0xdce4f0, 0xc8d2e2, 0xb0bccc);
    st.sky = gfx::texture(sky);
    st.lights.push_back({mx, my, 180, 0x6a7ab0, 0.75f, 0});

    // ---- Town layer
    Image t(VW, VH);
    // Mountains: two ranges with snow caps.
    auto range = [&](uint32_t rock, uint32_t snow, uint32_t snow2, int baseY, float hmin, float hmax, int peaks, uint32_t seed) {
        Image l(VW, VH);
        std::vector<std::array<float, 3>> pk;
        for (int i = 0; i < peaks; i++) pk.push_back({(i + rnd(0.1f, 0.9f)) * VW / float(peaks), rnd(hmin, hmax), rnd(0.55f, 0.9f)});
        for (int xx = 0; xx < VW; xx++) {
            float h = 0, cap = 0, px = 0;
            for (auto& p : pk) {
                float v = p[1] - std::fabs(xx - p[0]) * p[2];
                if (v > h) h = v, cap = p[1], px = p[0];
            }
            h += (vnoise(xx / 5.f, 0.5f, 256, seed) - 0.5f) * 5;
            int top = baseY - int(h);
            for (int y = top; y < 160; y++) {
                float depth = float(y - top);
                float snowLine = (cap * 0.35f) + (vnoise(xx / 3.f, y / 3.f, 256, seed + 1) - 0.5f) * 8;
                bool sn = depth < snowLine && (baseY - y) > cap * 0.45f;
                float side = clampf((xx - px) / 6.f + 0.5f, 0, 1); // 0 lit (moon side), 1 shadow
                uint32_t c = sn ? (side > bayer(xx, y) ? snow2 : snow) : rock;
                if (!sn && depth < snowLine + 5 && bayer(xx, y) < 0.12f) c = snow2;
                l.set(xx, y, argb(c));
            }
        }
        t.blit(l, 0, 0);
    };
    range(0x2c3c5e, 0x8a9cc0, 0x6a7ca0, 150, 30, 70, 5, 61);
    range(0x1e2a48, 0x6a7ca4, 0x4e5e86, 158, 16, 40, 7, 71);

    // The abbey: long hall, cloister, bell tower.
    const uint32_t AC = argb(0x141c30), AD = argb(0x0c1222);
    Image ab(VW, VH);
    {
        int b = 162;
        // Hall
        ab.fill(122, 120, 74, b - 120, AC);
        for (int i = 0; i < 18; i++) ab.fill(120 + i, 120 - i, 78 - i * 2, 1, AC);
        // Cloister wall with arches
        ab.fill(196, 136, 46, b - 136, AC);
        for (int i = 0; i < 5; i++) {
            int ax = 199 + i * 9;
            ab.fill(ax, 144, 5, 18, AD);
            ab.set(ax + 1, 143, AD), ab.set(ax + 2, 143, AD), ab.set(ax + 3, 143, AD);
        }
        // Bell tower
        int tx = 150, top = 64;
        ab.fill(tx, top, 20, b - top, AC);
        for (int i = 0; i < 22; i++) ab.fill(tx - 2 + i / 2, top - i, 24 - (i / 2) * 2, 1, AC);
        ab.fill(tx + 9, top - 30, 2, 9, AC);
        ab.fill(tx + 7, top - 27, 6, 2, AC);
        // Belfry opening with the bell
        ab.fill(tx + 5, top + 6, 10, 14, AD);
        for (int i = 0; i < 3; i++) ab.fill(tx + 6 + i, top + 5 - i, 8 - i * 2, 1, AD);
        ab.fill(tx + 9, top + 7, 2, 2, argb(0x3a3420));
        ab.fill(tx + 7, top + 9, 6, 5, argb(0x5a4c2a));
        ab.fill(tx + 6, top + 14, 8, 1, argb(0x5a4c2a));
        ab.fill(tx + 7, top + 9, 1, 5, argb(0x7a6a3a));
        ab.set(tx + 10, top + 15, argb(0x3a3420));
        // Small apse on the left
        ab.fill(106, 134, 16, b - 134, AC);
        for (int i = 0; i < 8; i++) ab.fill(106 + i, 134 - i, 16 - i, 1, AC);
        // Chimney
        ab.fill(182, 98, 4, 10, AC);
    }
    snowcap(ab, 0x9aaccc, 0x7a8cae, 3);
    t.blit(ab, 0, 0);
    // Warm windows
    auto warm = [&](int wx, int wy, int w, int h, uint32_t col) {
        t.fill(wx - 1, wy - 1, w + 2, h + 1, AD);
        t.set(wx + w / 2, wy - 2, AD);
        st.windows.push_back({float(wx), float(wy), float(w), float(h), col, rnd(0, 6.28f)});
    };
    warm(128, 130, 3, 8, 0xffb860);
    warm(138, 130, 3, 8, 0xffb860);
    warm(176, 130, 3, 8, 0xffa850);
    warm(186, 130, 3, 8, 0xffb860);
    warm(158, 94, 4, 6, 0xffc070);
    warm(112, 144, 2, 5, 0x8ab0ff);
    st.lights.push_back({158, 140, 56, 0xffa050, 0.45f, 0.1f});
    // Abbey door with a lantern
    t.fill(140, 146, 8, 16, AD);
    t.fill(141, 145, 6, 1, AD);
    st.windows.push_back({141, 148, 6, 12, 0xd08a40, 0.5f});

    // Mid layer: pines and a frozen well.
    const uint32_t MC = argb(0x0a1222);
    Image m(VW, VH);
    pine(m, 14, 164, 52, MC);
    pine(m, 40, 166, 38, MC);
    pine(m, 64, 164, 30, MC);
    pine(m, 92, 166, 44, MC);
    pine(m, 250, 166, 40, MC);
    pine(m, 272, 164, 54, MC);
    pine(m, 296, 166, 30, MC);
    for (int i = 0; i < 6; i++) pine(m, irnd(100, 240), 162, irnd(12, 20), argb(0x101a2e));
    // Graveyard crosses half buried in snow
    for (int i = 0; i < 5; i++) {
        int cx = 206 + i * 8 + irnd(-1, 1), ch = irnd(6, 9);
        m.fill(cx, 162 - ch, 1, ch, MC);
        m.fill(cx - 2, 162 - ch + 2, 5, 1, MC);
    }
    snowcap(m, 0x8a9ec0, 0x6a7e9e, 2);
    t.blit(m, 0, 0);
    // Snowdrift along the foot of everything
    for (int xx = 0; xx < VW; xx++) {
        int h = int(3 + vnoise(xx / 10.f, 0.5f, 64, 81) * 5);
        for (int y = 158 - h; y < 160; y++) t.set(xx, y, argb(y == 158 - h ? 0x9cb0d0 : (bayer(xx, y) < 0.3f ? 0x7488aa : 0x8498ba)));
    }

    // ---- Ground: packed snow with footprints and ice; a trodden path to the clinic.
    const int GH = VH - 158;
    Image g(VW, GH);
    for (int y = 0; y < GH; y++)
        for (int xx = 0; xx < VW; xx++) {
            float z = 60.f / (y + 10.f);
            float n = vnoise(xx / 14.f, z * 2.f, 256, 91) * 0.75f + vnoise(xx / 5.f, z * 5.f, 256, 92) * 0.25f;
            float c[3];
            toF(mixRGB(0x8a9cbc, 0x6a7c9e, clampf(y / float(GH), 0, 1) * 0.5f), c);
            if (n > 0.6f) towards(c, 0xb0c0dc, (n - 0.6f) * 2.f);
            if (n < 0.35f) towards(c, 0x5a6a8c, (0.35f - n) * 2.f);
            g.set(xx, y, argb(dither(c, xx, y, 24)));
        }
    g.fill(0, 0, VW, 1, argb(0xb4c4e0));
    g.fill(0, 3, VW, 1, argb(0x6a7a9a));
    // Ice patches
    for (int i = 0; i < 5; i++) {
        float px = rnd(20, 280), py = rnd(14, GH - 8), rx = rnd(10, 20), ry = rx * 0.24f;
        for (int yy = int(py - ry); yy <= int(py + ry); yy++)
            for (int xx = int(px - rx); xx <= int(px + rx); xx++) {
                float dx = (xx - px) / rx, dy = (yy - py) / ry;
                if (dx * dx + dy * dy <= 1) g.set(xx, yy, argb(dy < -0.2f ? 0x9ab8d4 : 0x7c9cc0));
            }
        g.fill(int(px - rx * 0.5f), int(py - 1), int(rx * 0.5f), 1, argb(0xe0f0ff));
        g.set(int(px + rx * 0.3f), int(py), argb(0xe0f0ff));
    }
    // Footprints wandering toward the clinic
    for (int k = 0; k < 2; k++) {
        float fy = k ? 22.f : 34.f;
        for (int xx = 6 + k * 13; xx < 290; xx += 9) {
            fy += rnd(-1.5f, 1.5f);
            int yy = int(fy) + ((xx / 9) % 2 ? 2 : 0);
            g.fill(xx, yy, 2, 1, argb(0x5a6a90));
            g.set(xx + 2, yy, argb(0x6a7a9e));
        }
    }
    // Trodden slush and cobbles near the clinic
    const uint32_t stones[] = {0x4a5470, 0x525c78, 0x444e6a, 0x5a6480};
    Image path(VW, GH);
    cobbles(path, 4, 290, VW, stones, 4, 0xc0d0ec, 0x8a9cbc, 0.3f);
    for (int y = 0; y < GH; y++)
        for (int xx = 280; xx < VW; xx++) {
            float edge = (xx - 288) / 20.f + vnoise(xx / 3.f, y / 3.f, 128, 19) * 0.7f - 0.35f;
            if (edge > bayer(xx, y) && (path.get(xx, y) >> 24)) {
                uint32_t c = path.get(xx, y);
                if (vnoise(xx / 4.f, y / 2.f, 128, 29) > 0.6f) c = argb(0x8a9cbc); // snow in the cracks
                g.set(xx, y, c);
            }
        }
    bakeLamp(t, g, st, 70, 0xe0f0ff, 0x9ab8ff, 0xb0d0ff, 0.6f);
    bakeLamp(t, g, st, 226, 0xe0f0ff, 0x9ab8ff, 0xb0d0ff, 2.2f);
    st.town = gfx::texture(t);
    st.ground = gfx::texture(g);
}

// ============================================================ Clouds

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

std::vector<Window> clinicWindows;

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
        clinicWindows.push_back({float(334 + x), float(100 + y), float(w), float(h), 0xffb050, rnd(0, 6.28f)});
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
    A.lamp = gfx::sprite(makeLampImage());
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

    // Co-op update props
    A.saw = spr({"..S.S..", ".SsSsS.", "SsSSSsS", ".SSoSS.", "SsSSSsS", ".SsSsS.", "..S.S.."},
                pal({{'S', 0xd8dce4}, {'s', 0x8a8e98}, {'o', 0x5a4030}}));
    A.leech = spr({".rRRr", "RRRRR", ".rRRN"}, pal({{'R', 0xc81e36}, {'r', 0x7a1020}, {'N', 0xeef2f8}}));
    A.gasCan = spr({".gIg.", "gVVVg", "VAVVV", "VVVVV", "VVVVV", "gVVVg"},
                   pal({{'g', 0x4a4e58}, {'I', 0x9aa0aa}, {'V', 0x7aff7a}, {'A', 0xd8ffd0}}));
    A.scythe = spr({"...IIII..", ".II....I.", "I.......I", ".....W..i", "....W....", "...W.....", "..W......", ".W.......",
                    "W........"},
                   pal({{'I', 0xd8dce8}, {'i', 0x7a7e8a}, {'W', 0x6a4a30}}));
    A.ping = spr({"..OOO..", "..OWO..", "..OWO..", "..OWO..", "OOOWOOO", ".OWWWO.", "..OWO..", "...O...", "......."},
                 pal({{'O', 0xf0c040}, {'W', 0xffffff}}));
    A.revive = spr({".RRR.", "RRWRR", "RWWWR", "RRWRR", ".RRR."}, pal({{'R', 0xd8203a}, {'W', 0xffffff}}));
    A.hammer = spr({"IIIII..", "IIIIIi.", ".iWi...", "..W....", "..W....", "..W....", "..w...."},
                   pal({{'I', 0xb0b4bc}, {'i', 0x6a6e78}, {'W', 0x9a6a3a}, {'w', 0x5a3a20}}));
    A.star = spr({"..W..", "..Y..", "WYWYW", "..Y..", "..W.."}, pal({{'W', 0xffffff}, {'Y', 0xffe080}}));
    A.chevron = spr({"WWW..", "..WWW", "WWW.."}, pal({{'W', 0xffffff}}));
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

    // Co-op update enemies
    Palette ghoulP = pal({{'G', 0x8e9c86}, {'H', 0xb4c0aa}, {'g', 0x5e6c5a}, {'d', 0x3c4838}, {'c', 0xe0d8c0}, {'e', 0xe8ff50}, {'m', 0x2a0a10},
                          {'t', 0xf0e8d0}, {'r', 0x4a3a2a}});
    pair2(A.ghoul, join(GHOUL_TOP, GHOUL_LEGS_A), join(GHOUL_TOP, GHOUL_LEGS_B), ghoulP);
    A.ghoulLeap = spr(GHOUL_LEAP, ghoulP);
    pair2(A.bat, BAT_A, BAT_B, pal({{'W', 0x3a2c40}, {'b', 0x5a4656}, {'e', 0xff2a2a}}));
    Palette knightP = pal({{'A', 0x4a4c58}, {'a', 0x2e2f38}, {'h', 0x7a7e8c}, {'b', 0x8a8478}, {'e', 0xff5020}, {'P', 0x8a1a1a},
                           {'c', 0x4a1418}, {'I', 0x8a8e98}, {'S', 0x5a3a24}, {'C', 0x9a2a1a}, {'L', 0x2a2018}, {'k', 0x1c1c24}});
    pair2(A.knight, join(KNIGHT_TOP, KNIGHT_LEGS_A), join(KNIGHT_TOP, KNIGHT_LEGS_B), knightP);
    pair2(A.cultist, CULTIST_A, CULTIST_B,
          pal({{'H', 0x4e3448}, {'h', 0x2e1e2c}, {'k', 0x120a10}, {'e', 0xffd040}, {'W', 0x6a4a2a}, {'c', 0x5a5a62},
               {'I', 0xb08a3a}, {'i', 0x6a5020}, {'Y', 0x9aff40}, {'b', 0x9a8a5a}, {'s', 0xb8a890}}));
    Palette bishopP = pal({{'O', 0xd8a838}, {'o', 0x8a6420}, {'R', 0x8a1a22}, {'r', 0x5a0e16}, {'W', 0xc8c0b0}, {'w', 0x8a8478},
                           {'S', 0xe0d8c0}, {'s', 0x9a907a}, {'k', 0x1a0a10}, {'e', 0xc0ff40}, {'t', 0x6a6050}, {'Y', 0xff4060},
                           {'h', 0xd0c8b0}});
    pair2(A.bishop, join(BISHOP_TOP, BISHOP_HEM_A), join(BISHOP_TOP, BISHOP_HEM_B), bishopP);
    Palette horseP = pal({{'B', 0xd8d0bc}, {'b', 0x8a8270}, {'k', 0x2a2430}, {'Y', 0x8affc8}, {'e', 0x8affe0}, {'C', 0x3e3a4e},
                          {'c', 0x262434}, {'S', 0xe8e0cc}, {'s', 0x9a927e}, {'t', 0x6a6458}, {'W', 0x6a4a30}, {'I', 0xc8ccd8},
                          {'i', 0x6a6e7a}, {'R', 0x5a2a1a}, {'h', 0xd0c8b0}});
    pair2(A.horseman, HORSE_A, HORSE_B, horseP);
    A.horsemanRear = spr(HORSE_REAR, horseP);

    StageArt& s0 = A.stage[0];
    buildSky0(s0);
    Image town0 = buildTown0(s0);
    Image ground0 = buildGround0();
    buildClinic();
    buildProps();
    bakeLamp(town0, ground0, s0, 96, 0xffe0a0, 0xffb060, 0xffb050, 0.0f);
    bakeLamp(town0, ground0, s0, 214, 0xffe0a0, 0xffb060, 0xffb050, 1.7f);
    s0.town = gfx::texture(town0);
    s0.ground = gfx::texture(ground0);
    buildFx();

    buildMarsh(A.stage[1]);
    buildCathedral(A.stage[2]);
    buildAbbey(A.stage[3]);
    for (auto& st : A.stage) st.windows.insert(st.windows.end(), clinicWindows.begin(), clinicWindows.end());
    rngState = 0x9E3779B9u ^ uint32_t(SDL_GetTicksNS());
}
