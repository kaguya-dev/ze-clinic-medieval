#pragma once
// Little-endian byte packing for the co-op protocol.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

constexpr const char* PROTO_VERSION = "ZC-COOP-1";

enum Msg : uint8_t {
    MSG_HELLO = 1, // C->H  str version, str name, u8 skin
    MSG_WELCOME,   // H->C  u8 slot, u8 mode
    MSG_REJECT,    // H->C  str reason
    MSG_LOBBY,     // H->C  lobby roster + settings
    MSG_LOBBY_SET, // C->H  u8 skin, u8 ready
    MSG_START,     // H->C  u8 startStage
    MSG_SNAP,      // H->C  world snapshot (unreliable)
    MSG_EVENTS,    // H->C  batch of cosmetic events (reliable)
    MSG_INPUT,     // C->H  continuous input (unreliable)
    MSG_ACTION,    // C->H  discrete action (reliable)
    MSG_CHAT,      // both  u8 slot, str text
    MSG_TO_LOBBY,  // H->C  run over, back to the lobby
};

struct W {
    std::vector<uint8_t> b;
    void u8(uint32_t v) { b.push_back(uint8_t(v)); }
    void u16(uint32_t v) { u8(v); u8(v >> 8); }
    void u32(uint32_t v) { u16(v); u16(v >> 16); }
    void i16(float v) {
        long q = std::lround(v);
        if (q < -32768) q = -32768;
        if (q > 32767) q = 32767;
        u16(uint32_t(uint16_t(int16_t(q))));
    }
    void f(float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        u32(u);
    }
    void q8(float v, float scale) { // unsigned, clamped
        float q = v * scale;
        u8(q < 0 ? 0 : (q > 255 ? 255 : uint32_t(q + 0.5f)));
    }
    void str(const std::string& s) {
        size_t n = s.size() > 255 ? 255 : s.size();
        u8(uint32_t(n));
        b.insert(b.end(), s.begin(), s.begin() + n);
    }
    void clear() { b.clear(); }
    size_t size() const { return b.size(); }
};

struct Rd {
    const uint8_t* p;
    size_t n, i = 0;
    bool bad = false;
    Rd(const uint8_t* data, size_t len) : p(data), n(len) {}
    explicit Rd(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
    bool ok() const { return !bad; }
    bool more() const { return !bad && i < n; }
    uint32_t u8() {
        if (i >= n) { bad = true; return 0; }
        return p[i++];
    }
    uint32_t u16() { uint32_t a = u8(); return a | (u8() << 8); }
    uint32_t u32() { uint32_t a = u16(); return a | (u16() << 16); }
    float i16() { return float(int16_t(uint16_t(u16()))); }
    float f() {
        uint32_t u = u32();
        float v;
        std::memcpy(&v, &u, 4);
        return std::isfinite(v) ? v : 0.f;
    }
    float q8(float scale) { return u8() / scale; }
    std::string str() {
        uint32_t len = u8();
        if (i + len > n) { bad = true; return {}; }
        std::string s(reinterpret_cast<const char*>(p + i), len);
        i += len;
        return s;
    }
};

// Discrete player actions sent reliably from clients (and applied directly on the host).
enum Act : uint8_t {
    ACT_RELOAD, ACT_FLASK, ACT_TRAP, ACT_TONIC, ACT_SWITCH, ACT_PING, ACT_BUY, ACT_READY, ACT_GIFT, ACT_SKIN,
};
