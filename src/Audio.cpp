#include "Audio.hpp"
#include "Util.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

namespace audio {
namespace {

constexpr int SR = 44100;
constexpr float TAU = 2 * PI;

struct Sound {
    std::vector<float> data;
    float rate = 1.f; // source sample rate / SR
};
enum Bus { BUS_SFX, BUS_MUSIC, BUS_AMB };
struct Voice {
    const Sound* s = nullptr;
    double pos = 0;
    float step = 1, vol = 1;
    bool loop = false;
    int tag = 0, bus = BUS_SFX;
    float gain = 1, target = 1; // crossfade envelope (loops only)
};

Sound sounds[SFX_COUNT];
Sound tracks[MUSIC_TRACKS];
bool trackReady[MUSIC_TRACKS] = {}; // guarded by mtx; the slow tracks are synthesised on a worker thread
std::thread musicWorker;
Sound ambLoops[5]; // indexed by Ambience; [AMB_NONE] stays empty
std::vector<Voice> voices;
std::mutex mtx;
SDL_AudioStream* stream = nullptr;
bool isMuted = false;
bool musicOn = false;
int curTrack = 1, curAmb = AMB_NONE;
float volMaster = 1, volMusic = 1, volSfx = 1;
constexpr int TAG_MUSIC = 1, TAG_AMB = 2;
constexpr float FADE_STEP = 1.f / SR; // ~1 s crossfades
constexpr float MUSIC_VOL = 0.32f;
const float AMB_VOL[5] = {0, 0.22f, 0.26f, 0.3f, 0.26f};

// Noise source. The music worker thread switches to its own xorshift state (thread_local) so it never
// touches util::rngState, which belongs to the game thread.
thread_local bool workerNoise = false;
thread_local uint32_t noiseState = 1;
float nz() {
    if (!workerNoise) return util::rnd() * 2 - 1;
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return (noiseState >> 8) * (2.0f / 16777216.0f) - 1;
}

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

void scale(Sound& s, float k) {
    for (float& v : s.data) v *= k;
}

// Feedback echo applied in place (extends the sound by `tail` seconds).
void echo(Sound& s, float delay, float fb, float tail) {
    int sr = int(SR * s.rate + 0.5f);
    int d = int(delay * sr);
    s.data.resize(s.data.size() + size_t(tail * sr), 0.f);
    for (size_t i = size_t(d); i < s.data.size(); i++) s.data[i] += s.data[i - d] * fb;
    normalize(s, 0.9f);
}

// RBJ biquad (band-pass / low-pass), enough for formants and swept filters.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void bandpass(float f, float q, int sr = SR) {
        float w = TAU * f / sr, al = std::sin(w) / (2 * q), a0 = 1 + al;
        b0 = al / a0, b1 = 0, b2 = -al / a0, a1 = -2 * std::cos(w) / a0, a2 = (1 - al) / a0;
    }
    void lowpass(float f, float q, int sr = SR) {
        float w = TAU * f / sr, al = std::sin(w) / (2 * q), cw = std::cos(w), a0 = 1 + al;
        b0 = (1 - cw) / 2 / a0, b1 = (1 - cw) / a0, b2 = b0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
    }
    float operator()(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
        return y;
    }
};

void SDLCALL callback(void*, SDL_AudioStream* st, int additional, int) {
    static std::vector<float> buf;
    int frames = additional / int(sizeof(float));
    if (frames <= 0) return;
    buf.assign(frames, 0.f);
    float master;
    {
        std::lock_guard<std::mutex> lk(mtx);
        master = volMaster;
        for (auto& v : voices) {
            if (!v.s) continue;
            const auto& d = v.s->data;
            size_t n = d.size();
            float busVol = v.bus == BUS_MUSIC ? volMusic : volSfx;
            for (int i = 0; i < frames; i++) {
                size_t ip = size_t(v.pos);
                if (ip + 1 >= n) {
                    if (!v.loop) { v.s = nullptr; break; }
                    v.pos -= double(n - 1);
                    ip = size_t(v.pos);
                }
                if (v.gain != v.target) {
                    v.gain = util::approach(v.gain, v.target, FADE_STEP);
                    if (v.gain <= 0 && v.target <= 0) { v.s = nullptr; break; }
                }
                float fr = float(v.pos - double(ip));
                buf[i] += (d[ip] * (1 - fr) + d[ip + 1] * fr) * v.vol * v.gain * busVol;
                v.pos += v.step;
            }
        }
        voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice& v) { return !v.s; }), voices.end());
    }
    float g = isMuted ? 0.f : 0.75f * master;
    for (float& x : buf) x = std::tanh(x * g);
    SDL_PutAudioStreamData(st, buf.data(), frames * int(sizeof(float)));
}

// ------------------------------------------------------------- Music

constexpr int MSR = 22050; // music / ambience sample rate

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

// Single-cycle wavetables so long additive voices stay cheap.
struct Table {
    std::vector<float> t;
    explicit Table(std::initializer_list<float> harm) : t(2048) {
        for (int i = 0; i < 2048; i++) {
            float v = 0, ph = TAU * i / 2048.f;
            int k = 1;
            for (float a : harm) v += a * std::sin(ph * k++);
            t[i] = v;
        }
        float m = 0;
        for (float v : t) m = std::max(m, std::fabs(v));
        for (float& v : t) v /= m;
    }
    float at(double ph) const { // ph in cycles
        double f = ph - std::floor(ph);
        float x = float(f * 2048);
        int i = int(x);
        float fr = x - i;
        return t[i & 2047] * (1 - fr) + t[(i + 1) & 2047] * fr;
    }
};
const Table& sineT() { static Table t({1}); return t; }
const Table& organT() { static Table t({1, 0.7f, 0.45f, 0.5f, 0.15f, 0.25f, 0, 0.12f}); return t; }
const Table& padT() { static Table t({1, 0.3f, 0.12f, 0.05f}); return t; }

// Sustained voice with attack/release; `vib` is vibrato depth in semitones.
void tone(std::vector<float>& out, size_t start, size_t len, float freq, float amp, const Table& tb, float att, float rel,
          float vib = 0, int sr = MSR) {
    size_t L = out.size();
    double ph = 0;
    for (size_t k = 0; k < len; k++) {
        float t = float(k) / sr, u = float(len - k) / sr;
        float env = std::min(1.f, t / att) * std::min(1.f, u / rel);
        float f = freq * (vib > 0 ? std::pow(2.f, vib / 12.f * std::sin(TAU * 5.f * t) * std::min(1.f, t)) : 1.f);
        ph += f / sr;
        out[(start + k) % L] += tb.at(ph) * env * amp;
    }
}

// Struck bell / chime with inharmonic partials.
void bell(std::vector<float>& out, size_t start, float freq, float amp, float decay, int sr = MSR) {
    const float p[5] = {1, 2.0f, 2.76f, 5.4f, 8.93f};
    const float a[5] = {1, 0.5f, 0.35f, 0.18f, 0.08f};
    size_t L = out.size(), len = size_t(sr * 4.f / decay);
    const Table& s = sineT();
    for (int h = 0; h < 5; h++) {
        double ph = 0;
        float d = decay * (1 + h * 0.7f);
        for (size_t k = 0; k < len; k++) {
            float t = float(k) / sr;
            ph += freq * p[h] / sr;
            out[(start + k) % L] += s.at(ph) * a[h] * amp * std::exp(-t * d) * std::min(1.f, t * 800);
        }
    }
}

void kick(std::vector<float>& out, size_t s0, float amp, int sr = MSR) {
    size_t L = out.size();
    for (int i = 0; i < sr / 4; i++) {
        float t = float(i) / sr;
        out[(s0 + i) % L] += std::sin(TAU * (48 + 40 * std::exp(-t * 30)) * t) * std::exp(-t * 14) * amp;
    }
}
void tom(std::vector<float>& out, size_t s0, float f, float amp, int sr = MSR) {
    size_t L = out.size();
    for (int i = 0; i < sr / 5; i++) {
        float t = float(i) / sr;
        out[(s0 + i) % L] += (std::sin(TAU * (f + f * std::exp(-t * 25)) * t) * 0.9f + nz() * 0.15f * std::exp(-t * 40)) *
                             std::exp(-t * 16) * amp;
    }
}
void snare(std::vector<float>& out, size_t s0, float amp, int sr = MSR) {
    size_t L = out.size();
    float lp = 0;
    for (int i = 0; i < sr / 5; i++) {
        float t = float(i) / sr, n = nz();
        lp += 0.5f * (n - lp);
        out[(s0 + i) % L] += ((n - lp * 0.6f) * std::exp(-t * 22) * 0.7f + std::sin(TAU * 190 * t) * std::exp(-t * 30) * 0.5f) * amp;
    }
}

Sound finishTrack(std::vector<float>& out, int sr = MSR, float peak = 0.8f) {
    Sound s;
    s.data = std::move(out);
    s.data.push_back(s.data[0]); // guard sample for interpolation at loop point
    s.rate = float(sr) / SR;
    // Peak-normalise, but keep every track near the loudness of the original lute piece.
    float m = 0;
    double e = 0;
    for (float v : s.data) m = std::max(m, std::fabs(v)), e += double(v) * v;
    float rms = float(std::sqrt(e / std::max<size_t>(1, s.data.size())));
    if (m > 0) {
        float k = std::min(peak / m, rms > 0 ? 0.085f / rms : peak / m);
        for (float& v : s.data) v *= k;
    }
    return s;
}

// The plague town lute piece. `calm` gives the slower, drumless title variation.
Sound makeLute(bool calm) {
    const int sr = MSR;
    const float bpm = calm ? 60.f : 72.f, beat = 60.f / bpm, eighth = beat / 2;
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
            if (calm && e % 2 == 1 && e != 3) continue;
            int note = ch[arp[e]] + 12;
            float amp = (e % 2 == 0 ? 0.28f : 0.2f) * (calm ? 0.85f : 1.f);
            pluck(out, int(b0 + size_t(e * eighth * sr)), midiHz(float(note)), amp, sr, 0.993f, calm);
        }
        pluck(out, int(b0), midiHz(float(ch[0] - 12)), 0.5f, sr, 0.997f, true);
        pluck(out, int(b0 + size_t(2 * beat * sr)), midiHz(float(ch[0] - 12)), 0.35f, sr, 0.997f, true);
        if (!calm)
            for (int k : {0, 2}) kick(out, b0 + size_t(k * beat * sr), 0.45f); // heartbeat drum
        // Dark pad drone: root + fifth with slow swell (spills into the next bar).
        float f1 = midiHz(float(ch[0] - 12)), f2 = midiHz(float(ch[2] - 12));
        size_t padLen = size_t(5.2f * beat * sr);
        for (size_t i = 0; i < padLen; i++) {
            float t = float(i) / sr, u = float(i) / padLen;
            float env = std::sin(PI * u);
            float v = std::sin(TAU * f1 * t) + 0.6f * std::sin(TAU * f2 * t) + 0.25f * std::sin(TAU * f1 * 2.003f * t);
            out[(b0 + i) % L] += v * env * (calm ? 0.08f : 0.06f);
        }
    }
    // Ghostly whistle melody on the second half.
    const int mel[8] = {69, 65, 67, 64, 69, 70, 72, 73};
    for (int i = 0; i < 8; i++) {
        size_t s0 = size_t((16 + i * 2) * beat * sr);
        size_t len = size_t(1.9f * beat * sr);
        float f = midiHz(float(mel[i] - (calm ? 12 : 0)));
        for (size_t k = 0; k < len; k++) {
            float t = float(k) / sr, u = float(k) / len;
            float env = std::min(1.f, u * 6) * std::min(1.f, (1 - u) * 3);
            float vib = std::sin(TAU * 5.f * t) * 0.006f * std::min(1.f, t * 2);
            out[(s0 + k) % L] += std::sin(TAU * f * (1 + vib) * t) * env * 0.09f;
        }
    }
    return finishTrack(out, sr);
}

// Blighted marsh: slow A minor, low drone, sparse plucks.
Sound makeMarsh() {
    const int sr = MSR;
    const float beat = 60.f / 54.f;
    const int bars = 8;
    size_t L = size_t(bars * 4 * beat * sr);
    std::vector<float> out(L, 0.f);
    // Am F Dm E (two bars each)
    const int roots[4] = {45, 41, 38, 40};
    const int third[4] = {48, 45, 41, 44};
    // Droning low A with a slowly beating fifth across the whole loop.
    {
        double p1 = 0, p2 = 0, p3 = 0;
        for (size_t i = 0; i < L; i++) {
            float t = float(i) / sr;
            float sw = 0.75f + 0.25f * std::sin(TAU * t / (L / float(sr)) * 2);
            p1 += midiHz(33) / sr, p2 += midiHz(40) * 1.002f / sr, p3 += midiHz(45) * 0.998f / sr;
            out[i] += (padT().at(p1) * 0.6f + padT().at(p2) * 0.35f + sineT().at(p3) * 0.2f) * 0.16f * sw;
        }
    }
    for (int c = 0; c < 4; c++) {
        size_t s0 = size_t(c * 8 * beat * sr), len = size_t(8.6f * beat * sr);
        tone(out, s0, len, midiHz(float(roots[c] + 12)), 0.05f, padT(), 1.5f, 1.5f);
        tone(out, s0, len, midiHz(float(third[c] + 12)), 0.04f, padT(), 2.f, 1.5f);
    }
    // Sparse plucks from A minor pentatonic, a pattern with gaps.
    const int scale[6] = {57, 60, 62, 64, 67, 69};
    const int pat[16] = {0, -1, 3, -1, -1, 2, -1, 4, 1, -1, -1, 5, -1, 3, -1, -1};
    for (int bar = 0; bar < bars; bar++)
        for (int b = 0; b < 4; b++) {
            int k = pat[(bar * 4 + b + bar) % 16];
            if (k < 0) continue;
            int note = scale[(k + bar) % 6] + (bar % 4 == 3 ? -12 : 0);
            pluck(out, int((bar * 4 + b) * beat * sr), midiHz(float(note)), 0.26f, sr, 0.995f, true);
            if ((bar + b) % 3 == 0) pluck(out, int((bar * 4 + b + 0.5f) * beat * sr), midiHz(float(note + 7)), 0.12f, sr, 0.994f, true);
        }
    // A distant, slow heartbeat
    for (int bar = 0; bar < bars; bar += 2) kick(out, size_t(bar * 4 * beat * sr), 0.3f);
    return finishTrack(out, sr);
}

// Burning cathedral: organ chords, racing minor arpeggios and drums.
Sound makeCathedral() {
    const int sr = MSR;
    const float beat = 60.f / 132.f, s16 = beat / 4;
    const int bars = 16;
    size_t L = size_t(bars * 4 * beat * sr);
    std::vector<float> out(L, 0.f);
    // C minor: Cm Ab Fm G | Cm Ab Bb G
    const int chords[8][3] = {{48, 51, 55}, {44, 48, 51}, {41, 44, 48}, {43, 47, 50},
                              {48, 51, 55}, {44, 48, 51}, {46, 50, 53}, {43, 47, 50}};
    const int arp[16] = {0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 4, 3, 2, 1, 2, 3};
    for (int bar = 0; bar < bars; bar++) {
        const int* ch = chords[bar % 8];
        size_t b0 = size_t(bar * 4 * beat * sr), barLen = size_t(4 * beat * sr);
        // Organ: chord + bass in octaves
        for (int k = 0; k < 3; k++) tone(out, b0, barLen, midiHz(float(ch[k] + 12)), 0.05f, organT(), 0.03f, 0.05f);
        tone(out, b0, barLen, midiHz(float(ch[0] - 12)), 0.09f, organT(), 0.02f, 0.05f);
        // Arpeggio
        if (bar >= 2)
            for (int e = 0; e < 16; e++) {
                int i = arp[e];
                int note = ch[i % 3] + 24 + (i / 3) * 12;
                pluck(out, int(b0 + size_t(e * s16 * sr)), midiHz(float(note)), e % 4 == 0 ? 0.2f : 0.13f, sr, 0.990f, false);
            }
        // Drums
        for (int b = 0; b < 4; b++) {
            size_t s0 = b0 + size_t(b * beat * sr);
            kick(out, s0, 0.55f);
            if (b % 2 == 1) snare(out, s0, 0.35f);
            if (bar % 4 == 3 && b >= 2) {
                tom(out, s0 + size_t(beat * 0.5f * sr), 110, 0.35f);
                tom(out, s0 + size_t(beat * 0.75f * sr), 90, 0.35f);
            } else {
                tom(out, s0 + size_t(beat * 0.5f * sr), 70, 0.18f);
            }
        }
    }
    // Choir-like lead on the second half
    const int mel[8] = {67, 68, 65, 67, 72, 68, 70, 71};
    for (int i = 0; i < 8; i++)
        tone(out, size_t((32 + i * 4) * beat * sr), size_t(3.8f * beat * sr), midiHz(float(mel[i])), 0.07f, padT(), 0.2f, 0.4f, 0.15f);
    return finishTrack(out, sr);
}

// Frozen abbey: sparse bells and chimes over a cold pad.
Sound makeAbbey() {
    const int sr = MSR;
    const float beat = 60.f / 66.f;
    const int bars = 8;
    size_t L = size_t(bars * 4 * beat * sr);
    std::vector<float> out(L, 0.f);
    // Em C G D  (two bars each)
    const int pads[4][3] = {{52, 59, 64}, {48, 55, 64}, {43, 55, 62}, {50, 57, 62}};
    for (int c = 0; c < 4; c++) {
        size_t s0 = size_t(c * 8 * beat * sr), len = size_t(9.f * beat * sr);
        for (int k = 0; k < 3; k++) tone(out, s0, len, midiHz(float(pads[c][k])), 0.045f, padT(), 2.5f, 2.5f, 0.05f);
        tone(out, s0, len, midiHz(float(pads[c][0] - 12)), 0.06f, sineT(), 2.f, 2.f);
    }
    // Chimes: E minor pentatonic, sparse
    const int scale[7] = {76, 79, 81, 83, 86, 88, 91};
    const int pat[32] = {0, -1, -1, 3, -1, 2, -1, -1, 4, -1, -1, -1, 1, -1, 5, -1,
                         -1, 3, -1, -1, 6, -1, 4, -1, -1, -1, 2, -1, 0, -1, -1, -1};
    for (int i = 0; i < bars * 4; i++) {
        int k = pat[i % 32];
        if (k < 0) continue;
        bell(out, size_t(i * beat * sr), midiHz(float(scale[k])), 0.12f, 2.2f);
        if (i % 8 == 5) bell(out, size_t((i + 0.5f) * beat * sr), midiHz(float(scale[(k + 2) % 7])), 0.06f, 2.5f);
    }
    // Deep abbey bell at the top of each phrase
    for (int bar = 0; bar < bars; bar += 4) bell(out, size_t(bar * 4 * beat * sr), midiHz(40), 0.35f, 0.6f);
    return finishTrack(out, sr);
}

// Boss: fast, driving low drums and a sawtooth bass ostinato in D minor.
Sound makeBoss() {
    const int sr = MSR;
    const float beat = 60.f / 152.f, e8 = beat / 2;
    const int bars = 16;
    size_t L = size_t(bars * 4 * beat * sr);
    std::vector<float> out(L, 0.f);
    const int bass[8] = {38, 38, 50, 38, 41, 38, 44, 43};
    const int roots[4] = {0, 0, -2, 1}; // D D C Eb shifts per 4-bar phrase
    for (int bar = 0; bar < bars; bar++) {
        size_t b0 = size_t(bar * 4 * beat * sr);
        int sh = roots[(bar / 2) % 4];
        for (int e = 0; e < 8; e++) {
            // Filtered saw bass
            size_t s0 = b0 + size_t(e * e8 * sr), len = size_t(e8 * 0.9f * sr);
            float f = midiHz(float(bass[e] + sh - 12));
            float ph = 0, lp = 0, lp2 = 0;
            for (size_t k = 0; k < len; k++) {
                float t = float(k) / sr;
                ph += f / sr;
                float saw = 2 * (ph - std::floor(ph)) - 1;
                float cut = 0.08f + 0.25f * std::exp(-t * 18);
                lp += cut * (saw - lp);
                lp2 += cut * (lp - lp2);
                out[(s0 + k) % L] += lp2 * 0.32f * std::min(1.f, (len - k) / 200.f);
            }
            kick(out, s0, e % 2 == 0 ? 0.6f : 0.25f);
            if (e % 4 == 2) snare(out, s0, 0.4f);
            if (bar % 4 == 3 && e >= 4) tom(out, s0, 140 - e * 10.f, 0.4f);
        }
        // Organ stabs on the off-beats
        if (bar % 2 == 1)
            for (int k : {0, 3, 7}) tone(out, b0 + size_t(beat * 2.5f * sr), size_t(beat * 0.8f * sr), midiHz(float(62 + sh + k)), 0.05f,
                                         organT(), 0.01f, 0.08f);
    }
    // Wailing lead on the second half
    const int mel[8] = {74, 77, 76, 74, 81, 80, 77, 76};
    for (int i = 0; i < 8; i++)
        tone(out, size_t((32 + i * 4) * beat * sr), size_t(3.6f * beat * sr), midiHz(float(mel[i] + roots[(i / 2) % 4])), 0.06f,
             organT(), 0.05f, 0.3f, 0.2f);
    return finishTrack(out, sr);
}

// ------------------------------------------------------------- Ambience

// Crossfades the loop seam and normalises.
Sound finishLoop(std::vector<float>& d, int sr, float peak) {
    int n = int(d.size()), fade = sr / 5;
    for (int i = 0; i < fade; i++) {
        float t = float(i) / fade;
        d[i] = d[i] * t + d[n - fade + i] * (1 - t);
    }
    d.resize(n - fade);
    Sound s;
    s.data = std::move(d);
    s.rate = float(sr) / SR;
    float m = 0;
    for (float v : s.data) m = std::max(m, std::fabs(v));
    if (m > 0)
        for (float& v : s.data) v *= peak / m;
    return s;
}

Sound makeRain() {
    const int sr = MSR;
    int n = int(2.5f * sr);
    std::vector<float> d(n);
    float a = 0, b = 0;
    for (int i = 0; i < n; i++) {
        float x = nz();
        a += 0.35f * (x - a);
        b += 0.02f * (x - b);
        d[i] = (a - b) * 0.6f;
    }
    for (int k = 0; k < 120; k++) {
        int p = util::irnd(0, n - 400);
        float amp = util::rnd(0.2f, 0.6f);
        for (int i = 0; i < 300; i++) d[p + i] += nz() * amp * std::exp(-i / 40.f);
    }
    return finishLoop(d, sr, 0.7f);
}

Sound makeWind() {
    const int sr = MSR;
    int n = int(7.f * sr);
    std::vector<float> d(n);
    Biquad bp, bp2;
    float lp = 0;
    for (int i = 0; i < n; i++) {
        float t = float(i) / sr;
        float gust = 0.55f + 0.3f * std::sin(TAU * t / 7.f * 2) + 0.15f * std::sin(TAU * t / 7.f * 5 + 1);
        if (i % 64 == 0) {
            bp.bandpass(380 + 260 * gust, 6.f, sr);
            bp2.bandpass(900 + 400 * std::sin(TAU * t / 7.f * 3), 9.f, sr);
        }
        float x = nz();
        lp += 0.05f * (x - lp);
        d[i] = (bp(x) * 0.9f + bp2(x) * 0.35f + lp * 0.8f) * gust;
    }
    return finishLoop(d, sr, 0.7f);
}

Sound makeFire() {
    const int sr = MSR;
    int n = int(4.f * sr);
    std::vector<float> d(n);
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float x = nz();
        lp += 0.03f * (x - lp);
        lp2 += 0.2f * (x - lp2);
        float t = float(i) / sr;
        d[i] = lp * 2.2f * (0.8f + 0.2f * std::sin(TAU * t * 0.75f)) + (lp2 - lp) * 0.12f;
    }
    // Crackles and pops
    for (int k = 0; k < 260; k++) {
        int p = util::irnd(0, n - 600);
        float amp = util::rnd(0.15f, 0.9f), dec = util::rnd(6, 30);
        bool pop = util::chance(0.08f);
        for (int i = 0; i < (pop ? 500 : 120); i++)
            d[p + i] += (pop ? std::sin(i * 0.25f) * 0.8f + nz() * 0.4f : nz()) * amp * std::exp(-i / dec);
    }
    return finishLoop(d, sr, 0.7f);
}

Sound makeMarshAmb() {
    const int sr = MSR;
    int n = int(6.f * sr);
    std::vector<float> d(n, 0.f);
    // Soft water / breeze bed
    float lp = 0;
    for (int i = 0; i < n; i++) {
        lp += 0.02f * (nz() - lp);
        d[i] = lp * 0.9f;
    }
    // Frogs: pulsed low croaks
    for (int k = 0; k < 9; k++) {
        int p = util::irnd(0, n - sr);
        float f = util::rnd(110, 190), pr = util::rnd(22, 34), len = util::rnd(0.18f, 0.4f), amp = util::rnd(0.25f, 0.5f);
        int reps = util::irnd(1, 3);
        for (int r = 0; r < reps; r++)
            for (int i = 0; i < int(len * sr); i++) {
                float t = float(i) / sr;
                float pulse = std::max(0.f, std::sin(TAU * pr * t));
                float v = (std::sin(TAU * f * t) + 0.5f * std::sin(TAU * f * 2.01f * t)) * pulse * std::sin(PI * t / len);
                int idx = p + r * int((len + 0.12f) * sr) + i;
                if (idx < n) d[idx] += v * amp;
            }
    }
    // Insects: high chirping trills
    for (int k = 0; k < 14; k++) {
        int p = util::irnd(0, n - sr / 2);
        float f = util::rnd(3800, 5200), len = util::rnd(0.15f, 0.35f);
        for (int i = 0; i < int(len * sr); i++) {
            float t = float(i) / sr;
            float trill = std::sin(TAU * 32 * t) > 0.2f ? 1.f : 0.f;
            d[p + i] += std::sin(TAU * f * t) * trill * std::sin(PI * t / len) * 0.07f;
        }
    }
    return finishLoop(d, sr, 0.7f);
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

    // ---- Co-op update
    // Heavy musket crack rolling off the rooftops.
    sounds[SFX_SNIPE] = gen(0.5f, [lp = 0.f, hp = 0.f](float t) mutable {
        float n = nz();
        lp += (0.1f + 0.6f * std::exp(-t * 60)) * (n - lp);
        hp += 0.5f * (n - hp);
        float crack = (n - hp) * std::exp(-t * 90) * 1.6f;
        return crack + lp * 1.5f * std::exp(-t * 11) + std::sin(TAU * (50 + 140 * std::exp(-t * 40)) * t) * std::exp(-t * 12) * 1.1f;
    });
    echo(sounds[SFX_SNIPE], 0.17f, 0.32f, 0.6f);
    // Electric crackle: jittering buzz plus sparks.
    sounds[SFX_ZAP] = gen(0.32f, [ph = 0.f, f = 300.f](float t) mutable {
        if (util::chance(0.004f)) f = util::rnd(140, 900);
        ph += f / SR;
        float sq = (ph - std::floor(ph)) < 0.5f ? 1.f : -1.f;
        float spark = util::chance(0.02f) ? nz() * 2 : 0.f;
        float env = std::min(1.f, t * 200) * std::exp(-t * 6) * (0.6f + 0.4f * std::sin(TAU * 60 * t));
        return (sq * 0.5f + nz() * 0.6f + spark) * env;
    });
    // Metallic whirr: a toothed spinning blade, launched with a thunk.
    sounds[SFX_SAW] = gen(0.5f, [ph = 0.f, ph2 = 0.f](float t) mutable {
        float f = 900 + 500 * (1 - std::exp(-t * 12));
        ph += f / SR;
        ph2 += f * 2.41f / SR;
        float teeth = 0.55f + 0.45f * std::sin(TAU * 70 * t);
        float ring = (std::sin(TAU * ph) + 0.5f * std::sin(TAU * ph2)) * teeth;
        float thunk = std::sin(TAU * (120 + 200 * std::exp(-t * 60)) * t) * std::exp(-t * 40);
        return ring * std::min(1.f, t * 60) * std::exp(-t * 4) * 0.6f + thunk + nz() * 0.15f * std::exp(-t * 30);
    });
    // Hissing puff of gas.
    sounds[SFX_GAS] = gen(0.6f, [lp = 0.f, lp2 = 0.f](float t) mutable {
        float n = nz();
        lp += 0.6f * (n - lp);
        lp2 += 0.04f * (n - lp2);
        float hiss = (n - lp * 0.7f) * std::min(1.f, t * 50) * std::exp(-t * 5);
        float puff = lp2 * 4 * std::exp(-t * 18);
        return hiss * 0.6f + puff;
    });
    // Wet squelch and pop.
    sounds[SFX_LEECH] = gen(0.25f, [lp = 0.f](float t) mutable {
        lp += 0.15f * (nz() - lp);
        float gl = std::sin(TAU * (420 * std::exp(-t * 14) + 90) * t) * std::exp(-t * 16);
        float pop = t < 0.006f ? nz() * (1 - t / 0.006f) : 0.f;
        return gl + lp * 2.5f * std::exp(-t * 20) * (0.5f + 0.5f * std::sin(TAU * 45 * t)) + pop;
    });
    // Tiny UI tick.
    sounds[SFX_HOVER] = gen(0.03f, [](float t) { return std::sin(TAU * 2600 * t) * std::exp(-t * 220); });
    scale(sounds[SFX_HOVER], 0.35f);
    // Bright ping chime.
    sounds[SFX_PING] = gen(0.7f, [](float t) {
        return (std::sin(TAU * 1760 * t) + 0.5f * std::sin(TAU * 2637 * t) * std::exp(-t * 4) + 0.3f * std::sin(TAU * 3520 * t) * std::exp(-t * 8)) *
               std::exp(-t * 5) * std::min(1.f, t * 1000);
    });
    // Rising holy chord.
    sounds[SFX_REVIVE] = gen(1.1f, [](float t) {
        const float f[4] = {523.3f, 659.3f, 784.f, 1046.5f};
        float v = 0;
        for (int i = 0; i < 4; i++) {
            float u = t - i * 0.09f;
            if (u < 0) continue;
            float g = 1 + 0.02f * u;
            v += (std::sin(TAU * f[i] * g * u) + 0.3f * std::sin(TAU * f[i] * 2 * g * u)) * std::min(1.f, u * 30) * std::exp(-u * 2.2f);
        }
        v += std::sin(TAU * 2093 * t) * 0.15f * std::sin(PI * t / 1.1f) * (0.5f + 0.5f * std::sin(TAU * 9 * t));
        return v;
    });
    // Falling tone ending in a body thud.
    sounds[SFX_DOWN] = gen(0.9f, [ph = 0.f, lp = 0.f, lp2 = 0.f](float t) mutable {
        ph += (320 * std::exp(-t * 2.5f) + 50) / SR;
        float saw = 2 * (ph - std::floor(ph)) - 1;
        lp += 0.1f * (saw - lp);
        float fall = lp * std::min(1.f, t * 40) * (t < 0.55f ? 1.f : std::exp(-(t - 0.55f) * 12));
        float u = t - 0.55f, thud = 0;
        if (u > 0) {
            lp2 += 0.08f * (nz() - lp2);
            thud = std::sin(TAU * (80 - 60 * u) * u) * std::exp(-u * 18) + lp2 * 2 * std::exp(-u * 25);
        }
        return fall * 0.8f + thud;
    });
    // Ghostly neigh: whinnying saw through an "ee/ah" formant pair, with echo.
    {
        Biquad f1, f2;
        f1.bandpass(800, 5);
        f2.bandpass(2200, 7);
        sounds[SFX_NEIGH] = gen(1.1f, [ph = 0.f, f1, f2](float t) mutable {
            float pitch = t < 0.15f ? 380 + 3600 * t : 920 * std::exp(-(t - 0.15f) * 0.9f);
            pitch *= 1 + 0.07f * std::sin(TAU * 13 * t) * std::min(1.f, t * 4);
            ph += pitch / SR;
            float saw = 2 * (ph - std::floor(ph)) - 1 + nz() * 0.25f;
            float env = std::min(1.f, t * 20) * std::exp(-t * 2.4f);
            return (f1(saw) + f2(saw) * 0.7f) * env;
        });
        echo(sounds[SFX_NEIGH], 0.21f, 0.35f, 0.5f);
    }
    // Four hoof beats.
    sounds[SFX_GALLOP] = gen(0.55f, [lp = 0.f](float t) mutable {
        float v = 0;
        const float ts[4] = {0.f, 0.08f, 0.17f, 0.34f};
        lp += 0.15f * (nz() - lp);
        for (int i = 0; i < 4; i++) {
            float u = t - ts[i];
            if (u < 0 || u > 0.12f) continue;
            v += (std::sin(TAU * (95 - 200 * u) * u) * 1.2f + lp * 1.5f) * std::exp(-u * 45) * (i == 3 ? 1.2f : 0.9f);
            if (u < 0.004f) v += nz() * 0.6f;
        }
        return v;
    });
    // Low choir "aah": detuned saws through vowel formants.
    {
        Biquad fa, fb, fc;
        fa.bandpass(700, 4);
        fb.bandpass(1150, 5);
        fc.bandpass(2600, 6);
        sounds[SFX_CHANT] = gen(1.3f, [p = std::array<float, 6>{}, fa, fb, fc](float t) mutable {
            const float fr[6] = {146.8f, 147.6f, 220.f, 219.2f, 293.7f, 174.6f};
            float s = 0;
            for (int i = 0; i < 6; i++) {
                p[i] += fr[i] * (1 + 0.004f * std::sin(TAU * (4.5f + i * 0.3f) * t)) / SR;
                s += 2 * (p[i] - std::floor(p[i])) - 1;
            }
            float env = std::min(1.f, t * 4) * std::min(1.f, (1.3f - t) * 3.5f);
            return (fa(s) + fb(s) * 0.6f + fc(s) * 0.2f) * env;
        });
    }
    // Roaring pillar of fire.
    {
        Biquad lpf;
        sounds[SFX_PILLAR] = gen(1.1f, [lpf, rum = 0.f, k = 0](float t) mutable {
            float sweep = t < 0.3f ? t / 0.3f : std::exp(-(t - 0.3f) * 2.5f);
            if ((k++ & 31) == 0) lpf.lowpass(300 + 2600 * sweep, 1.2f);
            float n = nz();
            rum += 0.01f * (n - rum);
            float crackle = util::chance(0.003f) ? nz() * 1.5f : 0.f;
            return lpf(n) * (0.4f + sweep) * std::min(1.f, t * 15) + rum * 5 * std::exp(-t * 1.5f) + crackle * std::exp(-t * 2);
        });
    }
    // Shield clang: inharmonic metal partials.
    sounds[SFX_CLANG] = gen(0.7f, [](float t) {
        const float f[5] = {523, 1247, 1873, 2731, 3520};
        const float d[5] = {5, 7, 9, 12, 16};
        float v = 0;
        for (int i = 0; i < 5; i++) v += std::sin(TAU * f[i] * t) * std::exp(-t * d[i]) / (1 + i * 0.3f);
        return v + (t < 0.01f ? nz() * (1 - t / 0.01f) * 1.5f : 0.f);
    });
    // Three rapid bat chirps.
    sounds[SFX_SCREECH] = gen(0.32f, [ph = 0.f](float t) mutable {
        float v = 0;
        for (int i = 0; i < 3; i++) {
            float u = t - i * 0.09f;
            if (u < 0 || u > 0.07f) continue;
            ph += (5200 - 30000 * u + 400 * std::sin(TAU * 90 * u)) / SR;
            v += (std::sin(TAU * ph) + 0.3f * std::sin(TAU * ph * 1.5f)) * std::sin(PI * u / 0.07f);
        }
        return v;
    });
    // Ghoul hiss/snarl.
    {
        Biquad bp;
        bp.bandpass(3000, 2);
        sounds[SFX_LEAP] = gen(0.45f, [bp, ph = 0.f, lp = 0.f](float t) mutable {
            ph += (95 + 30 * std::sin(TAU * 7 * t)) / SR;
            float saw = 2 * (ph - std::floor(ph)) - 1;
            lp += 0.2f * (saw - lp);
            float growl = lp * (0.5f + 0.5f * std::sin(TAU * 28 * t));
            float hiss = bp(nz());
            float env = std::min(1.f, t * 30) * std::exp(-t * 4.5f);
            return (growl * 0.7f + hiss * 1.2f) * env;
        });
    }
    // Soft healing shimmer.
    sounds[SFX_HEAL] = gen(0.7f, [](float t) {
        const float f[4] = {1318.5f, 1568, 1975.5f, 2637};
        float v = 0;
        for (int i = 0; i < 4; i++) {
            float u = t - i * 0.06f;
            if (u < 0) continue;
            v += std::sin(TAU * f[i] * u) * std::exp(-u * 5) * (0.6f + 0.4f * std::sin(TAU * (7 + i) * u)) * std::min(1.f, u * 60);
        }
        return v;
    });
    scale(sounds[SFX_HEAL], 0.55f);
    // Hammer knocks on wood.
    sounds[SFX_REPAIR] = gen(0.4f, [](float t) {
        float v = 0;
        for (float t0 : {0.f, 0.17f}) {
            float u = t - t0;
            if (u < 0) continue;
            v += (std::sin(TAU * 190 * u) * 0.9f + std::sin(TAU * 430 * u) * 0.6f * std::exp(-u * 30) + std::sin(TAU * 1250 * u) * 0.3f * std::exp(-u * 60)) *
                 std::exp(-u * 28);
            if (u < 0.003f) v += nz() * 0.8f;
        }
        return v;
    });
    // Friendly two-note chime.
    sounds[SFX_JOIN] = gen(0.5f, [](float t) {
        float v = 0;
        const float f[2] = {784, 1175};
        for (int i = 0; i < 2; i++) {
            float u = t - i * 0.12f;
            if (u >= 0) v += (std::sin(TAU * f[i] * u) + 0.35f * std::sin(TAU * f[i] * 2 * u)) * std::exp(-u * 7) * std::min(1.f, u * 400);
        }
        return v;
    });
    // Short confirmation blip.
    sounds[SFX_READY] = gen(0.13f, [](float t) {
        float f = t < 0.05f ? 1047.f : 1568.f;
        return (std::sin(TAU * f * t) > 0 ? 0.4f : -0.4f) * std::exp(-t * 18);
    });
    scale(sounds[SFX_READY], 0.7f);
}

void startLoop(int tag, int bus, const Sound* s, float vol) {
    for (auto& v : voices)
        if (v.tag == tag) v.target = 0; // fade out whatever is playing
    if (!s || s->data.empty()) return;
    Voice v;
    v.s = s;
    v.step = s->rate;
    v.vol = vol;
    v.loop = true;
    v.tag = tag;
    v.bus = bus;
    v.gain = 0;
    v.target = 1;
    voices.push_back(v);
}

// Publishes a finished track and starts it if it is the one we are waiting for.
void publishTrack(int i, Sound&& s) {
    std::lock_guard<std::mutex> lk(mtx);
    tracks[i] = std::move(s);
    trackReady[i] = true;
    bool playing = std::any_of(voices.begin(), voices.end(), [](const Voice& v) { return v.tag == TAG_MUSIC && v.target > 0; });
    if (stream && musicOn && curTrack == i && !playing) startLoop(TAG_MUSIC, BUS_MUSIC, &tracks[i], MUSIC_VOL);
}

} // namespace

bool init() {
    buildSounds();
    publishTrack(0, makeLute(true));
    publishTrack(1, makeLute(false));
    // The stage and boss tracks take a few hundred ms (more in debug builds): make them in the background.
    // The worker uses its own noise generator so it never races util::rngState with the game thread.
    musicWorker = std::thread([] {
        noiseState = 0x2545F491u;
        workerNoise = true;
        publishTrack(2, makeMarsh());
        publishTrack(3, makeCathedral());
        publishTrack(4, makeAbbey());
        publishTrack(5, makeBoss());
    });
    ambLoops[AMB_RAIN] = makeRain();
    ambLoops[AMB_WIND] = makeWind();
    ambLoops[AMB_FIRE] = makeFire();
    ambLoops[AMB_MARSH] = makeMarshAmb();
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
    if (musicWorker.joinable()) musicWorker.join();
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

void setMusic(bool on) {
    if (!stream) return;
    std::lock_guard<std::mutex> lk(mtx);
    if (on == musicOn) return;
    musicOn = on;
    startLoop(TAG_MUSIC, BUS_MUSIC, on && trackReady[curTrack] ? &tracks[curTrack] : nullptr, MUSIC_VOL);
}

void setMusicTrack(int track) {
    if (track < 0 || track >= MUSIC_TRACKS) return;
    std::lock_guard<std::mutex> lk(mtx);
    if (track == curTrack) return;
    curTrack = track;
    // An unfinished track fades the old one out now and starts as soon as the worker publishes it.
    if (stream && musicOn) startLoop(TAG_MUSIC, BUS_MUSIC, trackReady[curTrack] ? &tracks[curTrack] : nullptr, MUSIC_VOL);
}

void setAmbience(int amb) {
    if (amb < AMB_NONE || amb > AMB_MARSH) amb = AMB_NONE;
    if (!stream) return;
    std::lock_guard<std::mutex> lk(mtx);
    if (amb == curAmb) return;
    curAmb = amb;
    startLoop(TAG_AMB, BUS_AMB, amb == AMB_NONE ? nullptr : &ambLoops[amb], AMB_VOL[amb]);
}

void setRain(bool on) { setAmbience(on ? AMB_RAIN : AMB_NONE); }
void setMuted(bool m) { isMuted = m; }
bool muted() { return isMuted; }

void setVolumes(float master, float music, float sfx) {
    std::lock_guard<std::mutex> lk(mtx);
    volMaster = util::clampf(master, 0, 1);
    volMusic = util::clampf(music, 0, 1);
    volSfx = util::clampf(sfx, 0, 1);
}

} // namespace audio
