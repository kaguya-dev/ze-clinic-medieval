#pragma once

enum Sfx {
    SFX_PISTOL, SFX_BLUNDER, SFX_XBOW, SFX_FLAME, SFX_CANNON, SFX_EXPLODE, SFX_SQUEAK, SFX_SPLAT, SFX_COIN,
    SFX_RELOAD, SFX_PERFECT, SFX_JAM, SFX_HURT, SFX_THUD, SFX_CAW, SFX_BELL, SFX_BUY, SFX_CLICK, SFX_THUNDER,
    SFX_THROW, SFX_TRAP, SFX_SPIT, SFX_GROAN, SFX_DRINK, SFX_EMPTY, SFX_BREAK, SFX_COUNT
};

namespace audio {
bool init();
void shutdown();
void play(int id, float vol = 1.f, float pitch = 1.f);
void setMusic(bool on);
void setRain(bool on);
void setMuted(bool m);
bool muted();
} // namespace audio
