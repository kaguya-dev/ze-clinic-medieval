#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Virtual (pixel-art) resolution. Everything is drawn here and upscaled.
constexpr int VW = 384;
constexpr int VH = 216;

// Colours are 0xAARRGGBB, matching SDL_PIXELFORMAT_ARGB8888.
inline constexpr uint32_t argb(uint32_t rgb, uint32_t a = 255) { return (a << 24) | (rgb & 0xFFFFFF); }

struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    Image() = default;
    Image(int w_, int h_, uint32_t fill = 0) : w(w_), h(h_), px(size_t(w_) * h_, fill) {}
    uint32_t get(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h) ? 0 : px[size_t(y) * w + x]; }
    void set(int x, int y, uint32_t c) {
        if (x >= 0 && y >= 0 && x < w && y < h) px[size_t(y) * w + x] = c;
    }
    void blend(int x, int y, uint32_t c);
    void fill(int x, int y, int ww, int hh, uint32_t c);
    void blit(const Image& src, int dx, int dy);
    void line(int x0, int y0, int x1, int y1, uint32_t c);
    void disc(float cx, float cy, float r, uint32_t c);
};

using Palette = std::unordered_map<char, uint32_t>;

Image artToImage(const std::vector<std::string>& rows, const Palette& pal);
Image outlined(const Image& src, uint32_t color);      // grows the image by 1px on each side
Image bobFrame(const Image& src, int splitRow);       // shifts rows [0,split) down one pixel

struct GlowPx { int x, y; uint32_t col; bool strong; };

struct Sprite {
    std::vector<GlowPx> glow; // emissive pixels (outlined coords), drawn after lighting
    SDL_Texture* tex = nullptr;
    SDL_Texture* flash = nullptr; // white silhouette for hit flashes
    int w = 0, h = 0;
};

namespace gfx {
extern SDL_Renderer* R;

SDL_Texture* texture(const Image& img, SDL_BlendMode mode = SDL_BLENDMODE_BLEND, bool smoothScale = false);
Sprite sprite(const Image& img);

void fillRect(float x, float y, float w, float h, uint32_t c);
void rect(float x, float y, float w, float h, uint32_t c);
void line(float x0, float y0, float x1, float y1, uint32_t c);
void tex(SDL_Texture* t, float x, float y, float w, float h, uint32_t tint = 0xFFFFFFFF, SDL_BlendMode mode = SDL_BLENDMODE_BLEND);

// Top-left anchored.
void draw(const Sprite& s, float x, float y, bool flip = false, float scale = 1.f, bool flash = false, uint32_t tint = 0xFFFFFFFF);
// Bottom-centre (feet) anchored.
void drawFeet(const Sprite& s, float cx, float by, bool flip = false, float scale = 1.f, bool flash = false, uint32_t tint = 0xFFFFFFFF);
// Rotated around pivot (px,py) in sprite-local pixels; pivot lands on (wx,wy).
void drawRotated(const Sprite& s, float wx, float wy, float px, float py, float angleRad, bool flipV, float scale = 1.f,
                 uint32_t tint = 0xFFFFFFFF, SDL_BlendMode mode = SDL_BLENDMODE_BLEND);
} // namespace gfx

namespace font {
void init();
// align: 0 left, 1 centre, 2 right. outline draws a full dark border instead of a drop shadow.
void draw(const std::string& s, float x, float y, uint32_t color, int scale = 1, int align = 0, bool outline = false);
int width(const std::string& s, int scale = 1);
} // namespace font
