#include "Gfx.hpp"
#include "Util.hpp"

#include <cctype>
#include <cmath>

// ---------------------------------------------------------------- Image

void Image::blend(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint32_t a = c >> 24;
    if (!a) return;
    uint32_t& d = px[size_t(y) * w + x];
    if (a == 255) { d = c; return; }
    float fa = a / 255.f, fd = (d >> 24) / 255.f * (1 - fa), oa = fa + fd;
    auto ch = [&](int s) {
        float cs = float((c >> s) & 255), cd = float((d >> s) & 255);
        return uint32_t(oa > 0 ? (cs * fa + cd * fd) / oa : 0) & 255;
    };
    d = (uint32_t(oa * 255 + .5f) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

void Image::fill(int x0, int y0, int ww, int hh, uint32_t c) {
    for (int y = y0; y < y0 + hh; y++)
        for (int x = x0; x < x0 + ww; x++) blend(x, y, c);
}

void Image::blit(const Image& s, int dx, int dy) {
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++) {
            uint32_t c = s.px[size_t(y) * s.w + x];
            if (c >> 24) blend(dx + x, dy + y, c);
        }
}

void Image::line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        blend(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Image::disc(float cx, float cy, float r, uint32_t c) {
    for (int y = int(cy - r - 1); y <= int(cy + r + 1); y++)
        for (int x = int(cx - r - 1); x <= int(cx + r + 1); x++) {
            float dx = x + .5f - cx, dy = y + .5f - cy;
            if (dx * dx + dy * dy <= r * r) blend(x, y, c);
        }
}

Image artToImage(const std::vector<std::string>& rows, const Palette& pal) {
    int w = 0;
    for (auto& r : rows) w = std::max<int>(w, int(r.size()));
    Image img(w, int(rows.size()));
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < int(rows[y].size()); x++) {
            auto it = pal.find(rows[y][x]);
            if (it != pal.end()) img.set(x, y, it->second);
        }
    return img;
}

Image outlined(const Image& s, uint32_t col) {
    Image o(s.w + 2, s.h + 2);
    auto solid = [&](int x, int y) { return (s.get(x, y) >> 24) != 0; };
    for (int y = 0; y < o.h; y++)
        for (int x = 0; x < o.w; x++) {
            int sx = x - 1, sy = y - 1;
            if (solid(sx, sy)) o.set(x, y, s.get(sx, sy));
            else if (solid(sx - 1, sy) || solid(sx + 1, sy) || solid(sx, sy - 1) || solid(sx, sy + 1)) o.set(x, y, col);
        }
    return o;
}

Image bobFrame(const Image& s, int split) {
    Image o = s;
    for (int y = 0; y < split && y < s.h; y++)
        for (int x = 0; x < s.w; x++) o.set(x, y, y == 0 ? 0 : s.get(x, y - 1));
    return o;
}

// ---------------------------------------------------------------- Rendering helpers

namespace gfx {
SDL_Renderer* R = nullptr;

SDL_Texture* texture(const Image& img, SDL_BlendMode mode, bool smoothScale) {
    SDL_Texture* t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
    if (!t) {
        SDL_Log("texture failed: %s", SDL_GetError());
        return nullptr;
    }
    SDL_UpdateTexture(t, nullptr, img.px.data(), img.w * 4);
    SDL_SetTextureBlendMode(t, mode);
    SDL_SetTextureScaleMode(t, smoothScale ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    return t;
}

Sprite sprite(const Image& img) {
    Sprite s;
    s.w = img.w;
    s.h = img.h;
    s.tex = texture(img);
    Image f = img;
    for (auto& p : f.px)
        if (p >> 24) p = 0xFFFFFFFF;
    s.flash = texture(f);
    return s;
}

static void setDraw(uint32_t c) {
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, (c >> 16) & 255, (c >> 8) & 255, c & 255, c >> 24);
}

void fillRect(float x, float y, float w, float h, uint32_t c) {
    setDraw(c);
    SDL_FRect r{std::floor(x), std::floor(y), std::floor(w), std::floor(h)};
    SDL_RenderFillRect(R, &r);
}

void rect(float x, float y, float w, float h, uint32_t c) {
    fillRect(x, y, w, 1, c);
    fillRect(x, y + h - 1, w, 1, c);
    fillRect(x, y + 1, 1, h - 2, c);
    fillRect(x + w - 1, y + 1, 1, h - 2, c);
}

void line(float x0, float y0, float x1, float y1, uint32_t c) {
    setDraw(c);
    SDL_RenderLine(R, std::floor(x0) + .5f, std::floor(y0) + .5f, std::floor(x1) + .5f, std::floor(y1) + .5f);
}

static void tint(SDL_Texture* t, uint32_t c) {
    SDL_SetTextureColorMod(t, (c >> 16) & 255, (c >> 8) & 255, c & 255);
    SDL_SetTextureAlphaMod(t, c >> 24);
}

void tex(SDL_Texture* t, float x, float y, float w, float h, uint32_t c, SDL_BlendMode mode) {
    if (!t) return;
    tint(t, c);
    SDL_SetTextureBlendMode(t, mode);
    SDL_FRect d{x, y, w, h};
    SDL_RenderTexture(R, t, nullptr, &d);
}

void draw(const Sprite& s, float x, float y, bool flip, float scale, bool flash, uint32_t c) {
    SDL_Texture* t = flash ? s.flash : s.tex;
    if (!t) return;
    tint(t, c);
    SDL_FRect d{std::floor(x), std::floor(y), s.w * scale, s.h * scale};
    SDL_RenderTextureRotated(R, t, nullptr, &d, 0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

void drawFeet(const Sprite& s, float cx, float by, bool flip, float scale, bool flash, uint32_t c) {
    draw(s, cx - s.w * scale * 0.5f, by - s.h * scale, flip, scale, flash, c);
}

void drawRotated(const Sprite& s, float wx, float wy, float px, float py, float ang, bool flipV, float scale, uint32_t c,
                 SDL_BlendMode mode) {
    if (!s.tex) return;
    tint(s.tex, c);
    SDL_SetTextureBlendMode(s.tex, mode);
    float ppy = flipV ? (s.h - py) : py;
    SDL_FRect d{std::floor(wx) - px * scale, std::floor(wy) - ppy * scale, s.w * scale, s.h * scale};
    SDL_FPoint ctr{px * scale, ppy * scale};
    SDL_RenderTextureRotated(R, s.tex, nullptr, &d, ang * 180.0 / PI, &ctr, flipV ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
    SDL_SetTextureBlendMode(s.tex, SDL_BLENDMODE_BLEND);
}
} // namespace gfx

// ---------------------------------------------------------------- 3x5 pixel font

namespace font {
namespace {
SDL_Texture* atlas = nullptr;
int glyphIndex[128];

struct Glyph { char c; const char* rows[5]; };
const Glyph GLYPHS[] = {
    {'A', {".#.", "#.#", "###", "#.#", "#.#"}}, {'B', {"##.", "#.#", "##.", "#.#", "##."}},
    {'C', {".##", "#..", "#..", "#..", ".##"}}, {'D', {"##.", "#.#", "#.#", "#.#", "##."}},
    {'E', {"###", "#..", "##.", "#..", "###"}}, {'F', {"###", "#..", "##.", "#..", "#.."}},
    {'G', {".##", "#..", "#.#", "#.#", ".##"}}, {'H', {"#.#", "#.#", "###", "#.#", "#.#"}},
    {'I', {"###", ".#.", ".#.", ".#.", "###"}}, {'J', {"..#", "..#", "..#", "#.#", ".#."}},
    {'K', {"#.#", "#.#", "##.", "#.#", "#.#"}}, {'L', {"#..", "#..", "#..", "#..", "###"}},
    {'M', {"#.#", "###", "###", "#.#", "#.#"}}, {'N', {"##.", "#.#", "#.#", "#.#", "#.#"}},
    {'O', {".#.", "#.#", "#.#", "#.#", ".#."}}, {'P', {"##.", "#.#", "##.", "#..", "#.."}},
    {'Q', {".#.", "#.#", "#.#", "##.", ".##"}}, {'R', {"##.", "#.#", "##.", "#.#", "#.#"}},
    {'S', {".##", "#..", ".#.", "..#", "##."}}, {'T', {"###", ".#.", ".#.", ".#.", ".#."}},
    {'U', {"#.#", "#.#", "#.#", "#.#", "###"}}, {'V', {"#.#", "#.#", "#.#", "#.#", ".#."}},
    {'W', {"#.#", "#.#", "###", "###", "#.#"}}, {'X', {"#.#", "#.#", ".#.", "#.#", "#.#"}},
    {'Y', {"#.#", "#.#", ".#.", ".#.", ".#."}}, {'Z', {"###", "..#", ".#.", "#..", "###"}},
    {'0', {"###", "#.#", "#.#", "#.#", "###"}}, {'1', {".#.", "##.", ".#.", ".#.", "###"}},
    {'2', {"##.", "..#", ".#.", "#..", "###"}}, {'3', {"##.", "..#", ".#.", "..#", "##."}},
    {'4', {"#.#", "#.#", "###", "..#", "..#"}}, {'5', {"###", "#..", "##.", "..#", "##."}},
    {'6', {".##", "#..", "###", "#.#", "###"}}, {'7', {"###", "..#", ".#.", ".#.", ".#."}},
    {'8', {"###", "#.#", "###", "#.#", "###"}}, {'9', {"###", "#.#", "###", "..#", "##."}},
    {'.', {"...", "...", "...", "...", ".#."}}, {',', {"...", "...", "...", ".#.", "#.."}},
    {'!', {".#.", ".#.", ".#.", "...", ".#."}}, {'?', {"##.", "..#", ".#.", "...", ".#."}},
    {':', {"...", ".#.", "...", ".#.", "..."}}, {'-', {"...", "...", "###", "...", "..."}},
    {'+', {"...", ".#.", "###", ".#.", "..."}}, {'/', {"..#", "..#", ".#.", "#..", "#.."}},
    {'\'', {".#.", ".#.", "...", "...", "..."}}, {'(', {".#.", "#..", "#..", "#..", ".#."}},
    {')', {".#.", "..#", "..#", "..#", ".#."}}, {'%', {"#.#", "..#", ".#.", "#..", "#.#"}},
    {'<', {"..#", ".#.", "#..", ".#.", "..#"}}, {'>', {"#..", ".#.", "..#", ".#.", "#.."}},
    {'=', {"...", "###", "...", "###", "..."}}, {'*', {"#.#", ".#.", "#.#", "...", "..."}},
    {'&', {".#.", "#.#", ".#.", "##.", ".##"}}, {'#', {"#.#", "###", "#.#", "###", "#.#"}},
    {'[', {"##.", "#..", "#..", "#..", "##."}}, {']', {".##", "..#", "..#", "..#", ".##"}},
};
constexpr int GN = sizeof(GLYPHS) / sizeof(GLYPHS[0]);
} // namespace

void init() {
    for (int& g : glyphIndex) g = -1;
    Image img(GN * 4, 6);
    for (int i = 0; i < GN; i++) {
        glyphIndex[int(GLYPHS[i].c)] = i;
        for (int y = 0; y < 5; y++)
            for (int x = 0; x < 3; x++)
                if (GLYPHS[i].rows[y][x] == '#') img.set(i * 4 + x, y, 0xFFFFFFFF);
    }
    atlas = gfx::texture(img);
}

int width(const std::string& s, int scale) { return s.empty() ? 0 : (int(s.size()) * 4 - 1) * scale; }

static void raw(const std::string& s, float x, float y, uint32_t c, int scale) {
    SDL_SetTextureColorMod(atlas, (c >> 16) & 255, (c >> 8) & 255, c & 255);
    SDL_SetTextureAlphaMod(atlas, c >> 24);
    for (size_t i = 0; i < s.size(); i++) {
        int ch = std::toupper((unsigned char)s[i]);
        if (ch < 0 || ch >= 128 || glyphIndex[ch] < 0) continue;
        SDL_FRect src{float(glyphIndex[ch] * 4), 0, 3, 5};
        SDL_FRect dst{std::floor(x) + i * 4 * scale, std::floor(y), 3.f * scale, 5.f * scale};
        SDL_RenderTexture(gfx::R, atlas, &src, &dst);
    }
}

void draw(const std::string& s, float x, float y, uint32_t color, int scale, int align, bool outline) {
    if (align == 1) x -= width(s, scale) / 2;
    else if (align == 2) x -= width(s, scale);
    uint32_t dark = (color & 0xFF000000) | 0x0d0a12;
    if (outline) {
        for (int oy = -1; oy <= 1; oy++)
            for (int ox = -1; ox <= 1; ox++)
                if (ox || oy) raw(s, x + ox * scale, y + oy * scale, dark, scale);
    } else {
        raw(s, x + scale, y + scale, dark, scale);
    }
    raw(s, x, y, color, scale);
}
} // namespace font
