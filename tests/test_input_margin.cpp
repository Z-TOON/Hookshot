#include "../gpu/vk/input_margin.h"

#include <cstdio>
#include <vector>

#define CHECK(name, condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s at line %d\n", name, __LINE__); return 1; } } while (0)

namespace im = mkwvk::input_margin;

/// An image of image_w x image_h whose top left w x h holds the scene (each texel its own number, never 0) and whose margin
/// holds -1, an unwritten texel; the regions applied as a nearest blit does. Returns false when a copy reads outside the
/// scene, writes inside it, writes outside the image or writes a texel twice
static bool apply(uint32_t w, uint32_t h, uint32_t image_w, uint32_t image_h, std::vector<int>& px, uint32_t* count) {
    px.assign((size_t)image_w * image_h, -1);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) px[(size_t)y * image_w + x] = (int)(y * w + x + 1);
    const std::vector<int> before = px;
    std::vector<char> written(px.size(), 0);
    im::Region r[3];
    const uint32_t n = im::regions(w, h, image_w, image_h, r);
    *count = n;
    for (uint32_t i = 0; i < n; i++) {
        if (r[i].sx < 0 || r[i].sy < 0 || r[i].sw < 1 || r[i].sh < 1 || r[i].dw < 1 || r[i].dh < 1) return false;
        if (r[i].sx + r[i].sw > (int)w || r[i].sy + r[i].sh > (int)h) return false;            // (reads the scene alone)
        if (r[i].dx + r[i].dw > (int)image_w || r[i].dy + r[i].dh > (int)image_h) return false;   // (stays in the image)
        for (int y = 0; y < r[i].dh; y++)
            for (int x = 0; x < r[i].dw; x++) {
                const int dx = r[i].dx + x, dy = r[i].dy + y;
                if (dx < (int)w && dy < (int)h) return false;   // (never over the scene)
                const size_t at = (size_t)dy * image_w + dx;
                if (written[at]) return false;                  // (no two copies write one texel)
                written[at] = 1;
                // (nearest: the source texel whose centre the destination's maps to)
                const int sx = r[i].sx + x * r[i].sw / r[i].dw, sy = r[i].sy + y * r[i].sh / r[i].dh;
                px[at] = before[(size_t)sy * image_w + sx];
            }
    }
    return true;
}

int main() {
    std::vector<int> px;
    uint32_t n = 0;

    // the scene fills the image, or does not fit it: nothing to do
    im::Region r[3];
    CHECK("full", im::regions(1707, 960, 1707, 960, r) == 0);
    CHECK("too wide", im::regions(1708, 960, 1707, 960, r) == 0);
    CHECK("too tall", im::regions(1707, 961, 1707, 960, r) == 0);
    CHECK("empty", im::regions(0, 960, 1707, 960, r) == 0 && im::regions(1707, 0, 1707, 960, r) == 0);

    // DLSS at 2560 x 1440 with a 1707 x 960 scene: the report this came from. Every texel within kMargin of the scene's right
    // and bottom edges holds the scene's nearest texel, as a clamping sampler gives it
    {
        const uint32_t w = 1707, h = 960, iw = 2560, ih = 1440;
        CHECK("dlss applies", apply(w, h, iw, ih, px, &n));
        CHECK("dlss three", n == 3);
        for (uint32_t y = 0; y < h + im::kMargin; y++)
            for (uint32_t x = 0; x < w + im::kMargin; x++) {
                const uint32_t cx = x < w ? x : w - 1, cy = y < h ? y : h - 1;
                CHECK("dlss clamped", px[(size_t)y * iw + x] == (int)(cy * w + cx + 1));
            }
        // (and no further: the rest of the margin is left as it was)
        CHECK("dlss no further", px[(size_t)0 * iw + w + im::kMargin] == -1 && px[(size_t)(h + im::kMargin) * iw] == -1);
    }

    // a margin on one side only, and one narrower than kMargin
    CHECK("right only applies", apply(100, 50, 103, 50, px, &n) && n == 1);
    for (uint32_t y = 0; y < 50; y++)
        for (uint32_t x = 100; x < 103; x++) CHECK("right only", px[(size_t)y * 103 + x] == (int)(y * 100 + 99 + 1));
    CHECK("below only applies", apply(100, 50, 100, 52, px, &n) && n == 1);
    for (uint32_t y = 50; y < 52; y++)
        for (uint32_t x = 0; x < 100; x++) CHECK("below only", px[(size_t)y * 100 + x] == (int)(49 * 100 + x + 1));
    CHECK("one texel each applies", apply(7, 5, 8, 6, px, &n) && n == 3);
    CHECK("one texel corner", px[(size_t)5 * 8 + 7] == 5 * 7);

    // a one-texel scene, and every small size: the copies stay inside their rules
    CHECK("one texel scene", apply(1, 1, 32, 32, px, &n) && n == 3 && px[(size_t)8 * 32 + 8] == 1 && px[(size_t)9 * 32 + 9] == -1);
    for (uint32_t iw = 1; iw <= 20; iw++)
        for (uint32_t ih = 1; ih <= 20; ih++)
            for (uint32_t w = 1; w <= iw; w++)
                for (uint32_t h = 1; h <= ih; h++) {
                    CHECK("every small size", apply(w, h, iw, ih, px, &n));
                    CHECK("its count", n == (uint32_t)(w < iw) + (uint32_t)(h < ih) + (uint32_t)(w < iw && h < ih));
                }
    std::printf("PASS: the margin of an upscaler's inputs repeats the scene's edge\n");
    return 0;
}
