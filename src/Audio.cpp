#include "Audio.hpp"
#include "Util.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace audio {
namespace {

constexpr int SR = 44100;
constexpr float TAU = 2 * PI;

struct Sound {
    std::vector<float> data;
    float rate = 1.f; // source sample rate / SR
};
struct Voice {
    const Sound* s = nullptr;
    double pos = 0;
    float step = 1, vol = 1;
    bool loop = false;
    int tag = 0;
};

Sound sounds[SFX_COUNT];
Sound music, rainLoop;
std::vector<Voice> voices;
std::mutex mtx;
SDL_AudioStream* stream = nullptr;
bool isMuted = false;
constexpr int TAG_MUSIC = 1, TAG_RAIN = 2;

float nz() { return util::rnd() * 2 - 1; }

void normalize(Sound& s, float peak) {
    float m = 0;
    for (float v : s.data) m = std::max(m, std::fabs(v));
    if (m > 0)
        for (float& v : s.data) v *= peak / m;
    // Short fade out to avoid clicks.
    size_t f = std::min<size_t>(s.data.size(), 200);
    for (size_t i = 0; i < f; i++) s.data[s.data.size() - 1 - i] *= float(i) / f;
}

template <class F>
Sound gen(float dur, F f, int sr = SR) {
    Sound s;
    int n = int(dur * sr);
    s.data.resize(n);
    for (int i = 0; i < n; i++) s.data[i] = f(float(i) / sr);
    s.rate = float(sr) / SR;
    normalize(s, 0.9f);
    return s;
}

void SDLCALL callback(void*, SDL_AudioStream* st, int additional, int) {
    static std::vector<float> buf;
    int frames = additional / int(sizeof(float));
    if (frames <= 0) return;
    buf.assign(frames, 0.f);
    {
        std::lock_guard<std::mutex> lk(mtx);
        for (auto& v : voices) {
            if (!v.s) continue;
            const auto& d = v.s->data;
            size_t n = d.size();
            for (int i = 0; i < frames; i++) {
                size_t ip = size_t(v.pos);
                if (ip + 1 >= n) {
                    if (!v.loop) { v.s = nullptr; break; }
                    v.pos -= double(n - 1);
                    ip = size_t(v.pos);
                }
                float fr = float(v.pos - double(ip));
                buf[i] += (d[ip] * (1 - fr) + d[ip + 1] * fr) * v.vol;
                v.pos += v.step;
            }
        }
        voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice& v) { return !v.s; }), voices.end());
    }
    float g = isMuted ? 0.f : 0.75f;
    for (float& x : buf) x = std::tanh(x * g);
    SDL_PutAudioStreamData(st, buf.data(), frames * int(sizeof(float)));
}

// ------------------------------------------------------------- Music

float midiHz(float m) { return 440.f * std::pow(2.f, (m - 69.f) / 12.f); }

// Karplus-Strong plucked string; wraps around so the loop is seamless.
void pluck(std::vector<float>& out, int start, float freq, float amp, int sr, float decay, bool soft) {
    int N = std::max(2, int(sr / freq));
    std::vector<float> b(N);
    float lp = 0;
    for (auto& v : b) {
        float n = nz();
        lp += (soft ? 0.25f : 0.8f) * (n - lp);
        v = lp;
    }
    int dur = int(2.2f * sr), idx = 0;
    size_t L = out.size();
    for (int i = 0; i < dur; i++) {
        int nx = (idx + 1) % N;
        float v = b[idx];
        b[idx] = 0.5f * (b[idx] + b[nx]) * decay;
        idx = nx;
        float env = std::min(1.f, i / 40.f);
        out[(start + i) % L] += v * amp * env;
    }
}

Sound makeMusic() {
    const int sr = 22050;
    const float bpm = 72, beat = 60.f / bpm, eighth = beat / 2;
    const int bars = 8;
    size_t L = size_t(bars * 4 * beat * sr);
    std::vector<float> out(L, 0.f);
    // D minor: Dm Bb Gm A | Dm Bb C A
    const int chords[8][4] = {{50, 53, 57, 62}, {46, 50, 53, 58}, {43, 46, 50, 55}, {45, 49, 52, 57},
                              {50, 53, 57, 62}, {46, 50, 53, 58}, {48, 52, 55, 60}, {45, 49, 52, 57}};
    const int arp[8] = {0, 2, 1, 3, 2, 1, 3, 2};
    for (int bar = 0; bar < bars; bar++) {
        const int* ch = chords[bar];
        size_t b0 = size_t(bar * 4 * beat * sr);
        for (int e = 0; e < 8; e++) {
            int note = ch[arp[e]] + 12;
            float amp = (e % 2 == 0 ? 0.28f : 0.2f);
            pluck(out, int(b0 + size_t(e * eighth * sr)), midiHz(float(note)), amp, sr, 0.993f, false);
        }
        pluck(out, int(b0), midiHz(float(ch[0] - 12)), 0.5f, sr, 0.997f, true);
        pluck(out, int(b0 + size_t(2 * beat * sr)), midiHz(float(ch[0] - 12)), 0.35f, sr, 0.997f, true);
        // Heartbeat drum on beats 1 and 3
        for (int k : {0, 2}) {
            size_t s0 = b0 + size_t(k * beat * sr);
            for (int i = 0; i < sr / 4; i++) {
                float t = float(i) / sr;
                out[(s0 + i) % L] += std::sin(TAU * (48 + 40 * std::exp(-t * 30)) * t) * std::exp(-t * 14) * 0.45f;
            }
        }
        // Dark pad drone: root + fifth with slow swell (spills into the next bar).
        float f1 = midiHz(float(ch[0] - 12)), f2 = midiHz(float(ch[2] - 12));
        size_t padLen = size_t(5.2f * beat * sr);
        for (size_t i = 0; i < padLen; i++) {
            float t = float(i) / sr, u = float(i) / padLen;
            float env = std::sin(PI * u);
            float v = std::sin(TAU * f1 * t) + 0.6f * std::sin(TAU * f2 * t) + 0.25f * std::sin(TAU * f1 * 2.003f * t);
            out[(b0 + i) % L] += v * env * 0.06f;
        }
    }
    // Ghostly whistle melody on the second half.
    const int mel[8] = {69, 65, 67, 64, 69, 70, 72, 73};
    for (int i = 0; i < 8; i++) {
        size_t s0 = size_t((16 + i * 2) * beat * sr);
        size_t len = size_t(1.9f * beat * sr);
        float f = midiHz(float(mel[i]));
        for (size_t k = 0; k < len; k++) {
            float t = float(k) / sr, u = float(k) / len;
            float env = std::min(1.f, u * 6) * std::min(1.f, (1 - u) * 3);
            float vib = std::sin(TAU * 5.f * t) * 0.006f * std::min(1.f, t * 2);
            out[(s0 + k) % L] += std::sin(TAU * f * (1 + vib) * t) * env * 0.09f;
        }
    }
    Sound s;
    s.data = std::move(out);
    s.data.push_back(s.data[0]); // guard sample for interpolation at loop point
    s.rate = float(sr) / SR;
    float m = 0;
    for (float v : s.data) m = std::max(m, std::fabs(v));
    for (float& v : s.data) v *= 0.8f / m;
    return s;
}

Sound makeRain() {
    const int sr = 22050;
    int n = int(2.5f * sr);
    Sound s;
    s.data.resize(n);
    float a = 0, b = 0;
    for (int i = 0; i < n; i++) {
        float x = nz();
        a += 0.35f * (x - a);
        b += 0.02f * (x - b);
        s.data[i] = (a - b) * 0.6f;
    }
    for (int k = 0; k < 120; k++) {
        int p = util::irnd(0, n - 400);
        float amp = util::rnd(0.2f, 0.6f);
        for (int i = 0; i < 300; i++) s.data[p + i] += nz() * amp * std::exp(-i / 40.f);
    }
    int fade = sr / 5; // crossfade loop seam
    for (int i = 0; i < fade; i++) {
        float t = float(i) / fade;
        s.data[i] = s.data[i] * t + s.data[n - fade + i] * (1 - t);
    }
    s.data.resize(n - fade);
    s.rate = float(sr) / SR;
    float m = 0;
    for (float v : s.data) m = std::max(m, std::fabs(v));
    for (float& v : s.data) v *= 0.7f / m;
    return s;
}

void buildSounds() {
    sounds[SFX_PISTOL] = gen(0.4f, [lp = 0.f](float t) mutable {
        lp += (0.08f + 0.5f * std::exp(-t * 30)) * (nz() - lp);
        return lp * 1.4f * std::exp(-t * 14) + std::sin(TAU * (60 + 120 * std::exp(-t * 35)) * t) * std::exp(-t * 22) * 0.9f;
    });
    sounds[SFX_BLUNDER] = gen(0.7f, [lp = 0.f](float t) mutable {
        lp += (0.05f + 0.4f * std::exp(-t * 15)) * (nz() - lp);
        return lp * 1.6f * std::exp(-t * 7) + std::sin(TAU * (45 + 90 * std::exp(-t * 20)) * t) * std::exp(-t * 10);
    });
    sounds[SFX_XBOW] = gen(0.35f, [ph = 0.f](float t) mutable {
        ph += TAU * (170 + 260 * std::exp(-t * 40)) / SR;
        float tick = t < 0.01f ? nz() * (1 - t / 0.01f) : 0.f;
        return (std::sin(ph) + 0.35f * std::sin(2 * ph)) * std::exp(-t * 14) * 0.7f + tick;
    });
    sounds[SFX_FLAME] = gen(0.2f, [a = 0.f, b = 0.f](float t) mutable {
        float n = nz();
        a += 0.3f * (n - a);
        b += 0.04f * (n - b);
        return (a - b) * std::sin(PI * t / 0.2f);
    });
    sounds[SFX_CANNON] = gen(1.1f, [lp = 0.f](float t) mutable {
        lp += (0.03f + 0.35f * std::exp(-t * 10)) * (nz() - lp);
        float crackle = util::chance(0.002f) ? nz() * std::exp(-t * 3) : 0.f;
        return lp * 1.8f * std::exp(-t * 4) + std::sin(TAU * (38 + 60 * std::exp(-t * 8)) * t) * std::exp(-t * 5) * 1.2f + crackle;
    });
    sounds[SFX_EXPLODE] = gen(1.5f, [lp = 0.f, lp2 = 0.f](float t) mutable {
        lp += (0.02f + 0.3f * std::exp(-t * 6)) * (nz() - lp);
        lp2 += 0.01f * (lp - lp2);
        float crackle = util::chance(0.004f) ? nz() * std::exp(-t * 2.5f) * 0.6f : 0.f;
        return lp * 2.f * std::exp(-t * 3) + lp2 * 3 * std::exp(-t * 2) +
               std::sin(TAU * (30 + 50 * std::exp(-t * 6)) * t) * std::exp(-t * 4) + crackle;
    });
    sounds[SFX_SQUEAK] = gen(0.2f, [ph = 0.f](float t) mutable {
        ph += TAU * (2300 + 700 * std::sin(t * 90) - t * 3500) / SR;
        return (std::sin(ph) + 0.3f * std::sin(3 * ph)) * std::min(1.f, t * 80) * std::exp(-t * 12);
    });
    sounds[SFX_SPLAT] = gen(0.3f, [lp = 0.f](float t) mutable {
        lp += 0.12f * (nz() - lp);
        return lp * 2 * std::exp(-t * 18) + std::sin(TAU * 140 * t) * std::exp(-t * 25) * 0.6f;
    });
    sounds[SFX_COIN] = gen(0.18f, [](float t) {
        float f = t < 0.06f ? 988.f : 1319.f;
        return (std::sin(TAU * f * t) > 0 ? 0.4f : -0.4f) * std::exp(-t * 14);
    });
    sounds[SFX_RELOAD] = gen(0.3f, [lp = 0.f](float t) mutable {
        float v = 0;
        for (float t0 : {0.f, 0.14f}) {
            float u = t - t0;
            if (u >= 0 && u < 0.04f) {
                float n = nz();
                lp += 0.3f * (n - lp);
                v += (n - lp) * (1 - u / 0.04f) + std::sin(TAU * 2400 * u) * std::exp(-u * 80) * 0.5f;
            }
        }
        return v;
    });
    sounds[SFX_PERFECT] = gen(0.6f, [](float t) {
        float v = 0;
        const float f[3] = {1047, 1319, 1568};
        for (int i = 0; i < 3; i++) {
            float u = t - i * 0.07f;
            if (u >= 0) v += (std::sin(TAU * f[i] * u) + 0.4f * std::sin(TAU * f[i] * 2 * u)) * std::exp(-u * 7);
        }
        return v;
    });
    sounds[SFX_JAM] = gen(0.3f, [](float t) {
        return ((std::sin(TAU * 95 * t) > 0 ? 0.5f : -0.5f) + nz() * 0.3f) * std::exp(-t * 9);
    });
    sounds[SFX_HURT] = gen(0.35f, [ph = 0.f, lp = 0.f](float t) mutable {
        ph += (220 - 260 * t) / SR;
        float saw = 2 * (ph - std::floor(ph)) - 1;
        lp += 0.15f * (saw - lp);
        return (lp + nz() * 0.2f) * std::exp(-t * 7);
    });
    sounds[SFX_THUD] = gen(0.25f, [lp = 0.f](float t) mutable {
        lp += 0.08f * (nz() - lp);
        return std::sin(TAU * (90 - 120 * t) * t) * std::exp(-t * 20) + lp * 2.5f * std::exp(-t * 25);
    });
    sounds[SFX_CAW] = gen(0.45f, [ph = 0.f, lp = 0.f](float t) mutable {
        float v = 0;
        for (float t0 : {0.f, 0.24f}) {
            float u = t - t0;
            if (u >= 0 && u < 0.18f) {
                ph += (560 - 900 * u + 60 * std::sin(TAU * 40 * u)) / SR;
                float saw = 2 * (ph - std::floor(ph)) - 1;
                lp += 0.35f * (saw - lp);
                v = lp * std::sin(PI * u / 0.18f);
            }
        }
        return v;
    });
    sounds[SFX_BELL] = gen(3.5f, [](float t) {
        const float p[8] = {0.5f, 1, 1.19f, 1.5f, 2, 2.5f, 3, 4.2f};
        const float a[8] = {0.6f, 1, 0.6f, 0.4f, 0.5f, 0.25f, 0.2f, 0.1f};
        const float d[8] = {0.6f, 0.8f, 1.2f, 1.5f, 1.8f, 2.5f, 3, 4};
        float v = 0;
        for (int i = 0; i < 8; i++) v += std::sin(TAU * 196 * p[i] * t) * a[i] * std::exp(-t * d[i]);
        return v * std::min(1.f, t * 400);
    });
    sounds[SFX_BUY] = gen(0.32f, [](float t) {
        float v = 0;
        const float f[3] = {880, 1175, 1568};
        for (int i = 0; i < 3; i++) {
            float u = t - i * 0.07f;
            if (u >= 0) v += (std::sin(TAU * f[i] * u) > 0 ? 0.3f : -0.3f) * std::exp(-u * 18);
        }
        return v;
    });
    sounds[SFX_CLICK] = gen(0.05f, [](float t) { return (std::sin(TAU * 1800 * t) > 0 ? 0.4f : -0.4f) * std::exp(-t * 90); });
    sounds[SFX_THUNDER] = gen(3.2f, [lp = 0.f, lp2 = 0.f](float t) mutable {
        float n = nz();
        lp += 0.03f * (n - lp);
        lp2 += 0.4f * (n - lp2);
        float rumble = 0.6f + 0.4f * std::sin(t * 7 + std::sin(t * 3) * 2);
        return lp * 3 * std::exp(-t * 1.1f) * rumble + lp2 * std::exp(-t * 18) * 0.6f;
    });
    sounds[SFX_THROW] = gen(0.25f, [a = 0.f, b = 0.f](float t) mutable {
        float n = nz();
        a += (0.05f + t) * (n - a);
        b += 0.02f * (n - b);
        return (a - b) * std::sin(PI * t / 0.25f);
    });
    sounds[SFX_TRAP] = gen(0.45f, [lp = 0.f](float t) mutable {
        float n = nz();
        lp += 0.4f * (n - lp);
        return (n - lp) * std::exp(-t * 60) * 1.5f + std::sin(TAU * 1800 * t) * std::exp(-t * 12) * 0.4f +
               std::sin(TAU * 2730 * t) * std::exp(-t * 15) * 0.3f;
    });
    sounds[SFX_SPIT] = gen(0.3f, [lp = 0.f](float t) mutable {
        lp += 0.1f * (nz() - lp);
        return lp * 3 * (0.5f + 0.5f * std::sin(TAU * 35 * t)) * std::exp(-t * 9);
    });
    sounds[SFX_GROAN] = gen(0.8f, [ph = 0.f, lp = 0.f](float t) mutable {
        ph += (85 + 8 * std::sin(TAU * 4 * t) - 25 * t) / SR;
        float saw = 2 * (ph - std::floor(ph)) - 1;
        lp += 0.06f * (saw - lp);
        return lp * std::sqrt(std::sin(PI * t / 0.8f));
    });
    sounds[SFX_DRINK] = gen(0.5f, [](float t) {
        float v = 0;
        for (int i = 0; i < 3; i++) {
            float u = t - i * 0.15f;
            if (u >= 0 && u < 0.1f) v += std::sin(TAU * (320 - 1500 * u) * u) * std::sin(PI * u / 0.1f);
        }
        return v;
    });
    sounds[SFX_EMPTY] = gen(0.06f, [](float t) { return nz() * std::exp(-t * 120) + std::sin(TAU * 3000 * t) * std::exp(-t * 90) * 0.4f; });
    sounds[SFX_BREAK] = gen(0.9f, [lp = 0.f](float t) mutable {
        lp += 0.2f * (nz() - lp);
        float splinter = util::chance(0.01f) ? nz() * std::exp(-t * 4) : 0.f;
        return lp * 2 * std::exp(-t * 6) + splinter + std::sin(TAU * (70 - 40 * t) * t) * std::exp(-t * 8);
    });
}

} // namespace

bool init() {
    buildSounds();
    music = makeMusic();
    rainLoop = makeRain();
    SDL_AudioSpec spec{SDL_AUDIO_F32, 1, SR};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, nullptr);
    if (!stream) {
        SDL_Log("Audio disabled: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return true;
}

void shutdown() {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = nullptr;
}

void play(int id, float vol, float pitch) {
    if (!stream || id < 0 || id >= SFX_COUNT) return;
    std::lock_guard<std::mutex> lk(mtx);
    if (voices.size() > 48) {
        auto it = std::find_if(voices.begin(), voices.end(), [](const Voice& v) { return !v.loop; });
        if (it != voices.end()) voices.erase(it);
    }
    Voice v;
    v.s = &sounds[id];
    v.step = sounds[id].rate * pitch;
    v.vol = vol;
    voices.push_back(v);
}

static void setLoop(int tag, const Sound* s, float vol, bool on) {
    if (!stream) return;
    std::lock_guard<std::mutex> lk(mtx);
    bool has = std::any_of(voices.begin(), voices.end(), [&](const Voice& v) { return v.tag == tag; });
    if (on && !has) {
        Voice v;
        v.s = s;
        v.step = s->rate;
        v.vol = vol;
        v.loop = true;
        v.tag = tag;
        voices.push_back(v);
    } else if (!on && has) {
        voices.erase(std::remove_if(voices.begin(), voices.end(), [&](const Voice& v) { return v.tag == tag; }), voices.end());
    }
}

void setMusic(bool on) { setLoop(TAG_MUSIC, &music, 0.32f, on); }
void setRain(bool on) { setLoop(TAG_RAIN, &rainLoop, 0.22f, on); }
void setMuted(bool m) { isMuted = m; }
bool muted() { return isMuted; }

} // namespace audio
