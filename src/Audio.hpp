#pragma once

enum Sfx {
    SFX_PISTOL, SFX_BLUNDER, SFX_XBOW, SFX_FLAME, SFX_CANNON, SFX_EXPLODE, SFX_SQUEAK, SFX_SPLAT, SFX_COIN,
    SFX_RELOAD, SFX_PERFECT, SFX_JAM, SFX_HURT, SFX_THUD, SFX_CAW, SFX_BELL, SFX_BUY, SFX_CLICK, SFX_THUNDER,
    SFX_THROW, SFX_TRAP, SFX_SPIT, SFX_GROAN, SFX_DRINK, SFX_EMPTY, SFX_BREAK,
    // Added for the co-op update
    SFX_SNIPE, SFX_ZAP, SFX_SAW, SFX_GAS, SFX_LEECH, SFX_HOVER, SFX_PING, SFX_REVIVE, SFX_DOWN, SFX_NEIGH,
    SFX_GALLOP, SFX_CHANT, SFX_PILLAR, SFX_CLANG, SFX_SCREECH, SFX_LEAP, SFX_HEAL, SFX_REPAIR, SFX_JOIN, SFX_READY,
    SFX_COUNT
};

// Music tracks: 0 title, 1 plague town, 2 blighted marsh, 3 burning cathedral, 4 frozen abbey, 5 boss.
constexpr int MUSIC_TRACKS = 6;
// Ambience loops: 0 none, 1 rain, 2 wind, 3 fire crackle, 4 marsh (frogs, insects).
enum Ambience { AMB_NONE, AMB_RAIN, AMB_WIND, AMB_FIRE, AMB_MARSH };

namespace audio {
bool init();
void shutdown();
void play(int id, float vol = 1.f, float pitch = 1.f);
void setMusic(bool on);
void setMusicTrack(int track); // crossfades to the given track (no-op if already playing)
void setAmbience(int amb);     // crossfades the ambience loop
void setRain(bool on);         // shorthand for setAmbience(on ? AMB_RAIN : AMB_NONE)
void setMuted(bool m);
bool muted();
void setVolumes(float master, float music, float sfx); // each in [0,1]
} // namespace audio
