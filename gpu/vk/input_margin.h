/* The margin of a temporal upscaler's inputs (vk_temporal.cpp pad_input), as plain rectangles: no Vulkan.
   tests/test_input_margin.py

   An engine's inputs are as large as the largest render size it takes (NVIDIA DLSS for a 2560 x 1440 output: 2560 x 1440 for a
   1707 x 960 scene; AMD FSR 4: the largest scene it has had), and the scene is written into their top left. What lies past it
   is read all the same: an upscaler's filters reach a few texels past the scene's last column and row. That margin was never
   written, so it held whatever the video memory had before (coloured lines along the picture's right and bottom edges), or,
   once the dynamic resolution stepped down, an earlier and larger frame's picture.

   The scene's last column and row are repeated into it, kMargin texels wide: what a clamping sampler would give. */
#pragma once

#include <algorithm>
#include <cstdint>

namespace mkwvk::input_margin {

/// How far the edge is repeated: past the widest filter an upscaler reads its inputs with (a 3 x 3 dilation after a bicubic
/// tap, a few texels), and one compute group's worth, so nothing this fills is ever more than a sliver of the image
constexpr uint32_t kMargin = 8;

/// A copy of sw x sh texels at (sx, sy), stretched with the nearest filter over dw x dh at (dx, dy)
struct Region {
    int32_t sx, sy, sw, sh;
    int32_t dx, dy, dw, dh;
};

/// The copies that fill the margin of an image_w x image_h input whose top left w x h holds the scene: the last column to the
/// right, the last row below and the corner texel into the corner, each at most kMargin wide and never past the image.
/// Every source lies inside the scene and every destination outside it, and no two destinations overlap: they can go in one
/// command with nothing ordering them. Returns how many (0 to 3): none when the scene fills the image, or does not fit it
inline uint32_t regions(uint32_t w, uint32_t h, uint32_t image_w, uint32_t image_h, Region out[3]) {
    if (!w || !h || w > image_w || h > image_h) return 0;
    const int32_t sw = (int32_t)w, sh = (int32_t)h;
    const int32_t px = (int32_t)std::min(kMargin, image_w - w), py = (int32_t)std::min(kMargin, image_h - h);
    uint32_t n = 0;
    if (px) out[n++] = {sw - 1, 0, 1, sh, sw, 0, px, sh};
    if (py) out[n++] = {0, sh - 1, sw, 1, 0, sh, sw, py};
    if (px && py) out[n++] = {sw - 1, sh - 1, 1, 1, sw, sh, px, py};
    return n;
}

} // namespace mkwvk::input_margin
