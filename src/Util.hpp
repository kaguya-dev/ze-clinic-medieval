#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

constexpr float PI = 3.14159265358979f;

namespace util {

inline uint32_t rngState = 0x9E3779B9u;

inline uint32_t xr() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}
inline float rnd() { return (xr() >> 8) * (1.0f / 16777216.0f); }
inline float rnd(float a, float b) { return a + (b - a) * rnd(); }
inline int irnd(int a, int b) { return a + int(xr() % uint32_t(b - a + 1)); }
inline bool chance(float p) { return rnd() < p; }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float approach(float v, float target, float d) {
    if (v < target) return std::min(v + d, target);
    return std::max(v - d, target);
}
inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

// Small seedable generator for effects that must look the same on every machine.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x1234567u) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float f() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float f(float a, float b) { return a + (b - a) * f(); }
};

// 4x4 ordered-dither threshold in [0,1).
inline float bayer(int x, int y) {
    static const int m[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    return (m[y & 3][x & 3] + 0.5f) / 16.0f;
}

// Linear blend of two 0xRRGGBB colours (alpha of result is opaque).
inline uint32_t mixRGB(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) {
        float va = float((a >> s) & 255), vb = float((b >> s) & 255);
        return uint32_t(clampf(va + (vb - va) * t, 0, 255)) << s;
    };
    return ch(16) | ch(8) | ch(0);
}

} // namespace util
