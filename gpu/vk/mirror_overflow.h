// (hookshot) A busy pool the mirrors' budget refuses takes room past it, while the card has it (RX_VK_MIRROR_OVERFLOW).
//
// The mirrors' budget is a size: a quarter of the card, or the preset's (1024 MB at Balanced, for 8 GB cards). Full, the next
// pool stays in system memory and the GPU reads it over PCIe, "slower, not wrong". How much slower depends on the pool. In
// Hyrule Field on an RTX 2080 (8 GB) the pool refused was the one the scene's culling runs in: 152 MB at 0x1178400000, bound
// 5,500 times in 600 submissions, read and written by five indirect compute dispatches of a few tens of thousands of threads
// each that then took 1.1 to 3.6 ms apiece. The GPU's frame was 27 ms at 683 x 384 as at 1536 x 864, 27 fps whatever the
// Render resolution, with 1.1 GB of the driver's allowance unused. With that pool mirrored: 11.7 ms and 57 fps.
//
// The mirrors that filled the budget were not idle (bound within 600 submissions) or held what the GPU alone wrote, so
// none could go for it (mirrors_give_back). The budget cannot tell a pool bound once a frame from one bound in every pass.
//
// So a pool refused for the budget counts the submissions that bound it. Bound in most of them for a while, it is made past
// the budget: by half the budget at most in all, and only while the driver's count with it stays under kRoomPct of what the
// driver allows. The textures' budget holds the count at 85% (vram_compact.h) and gives way by itself as the count rises.
// Rules only, no Vulkan: vk_memory.cpp mirror_make does it. tests/test_mirror_overflow.py runs them.
#pragma once

#include <cstdint>

namespace mirror_overflow {

inline constexpr uint64_t kHot = 120;       // (submissions that bound it since it was first refused: two seconds of play)
inline constexpr uint64_t kWindow = 1200;   // (a count older than this starts over: bound now and then is not busy)
// (the driver's count with the mirror, of what it allows, at most. Under the 97% at which the textures' budget is squeezed whatever
// the blocks hold (vk_texture.cpp), and over the 85% it is held at: with a browser, a chat client and a video player open the
// same card allowed 6.3 GB instead of 7.1 and the count sat at 91%; at 92 the 152 MB pool was refused there and the scene was
// back at 21 fps)
inline constexpr uint64_t kRoomPct = 96;

/// What a pool keeps between refusals
struct Count {
    uint64_t first = 0;   // (the submission of the first refusal counted; 0: none)
    uint64_t last = 0;    // (the submission last counted: a pool is wanted once a submission, counted once)
    uint32_t n = 0;       // (the submissions that bound it and found no room)
};

/// One more refusal, at submission `epoch`. Counted once a submission; a window that ran out starts over
inline void note(Count& c, uint64_t epoch) {
    if (c.first && epoch - c.first > kWindow) c = {};
    if (c.last == epoch && c.n) return;
    if (!c.first) c.first = epoch;
    c.last = epoch, c.n++;
}

/// Busy: refused in kHot submissions, and bound in at least half of the submissions since the first of them
inline bool busy(const Count& c, uint64_t epoch) {
    if (!c.first || c.n < kHot || epoch < c.first) return false;
    return (uint64_t)c.n * 2 >= epoch - c.first + 1;
}

/// The most the mirrors may hold with the pools made past their budget: half as much again
inline uint64_t ceiling(uint64_t budget) { return budget + budget / 2; }

/// Whether a refused pool of n bytes is made all the same: busy, within the ceiling, and with room on the card (the driver's
/// count known, and with the mirror not past kRoomPct of what it allows)
inline bool allowed(const Count& c, uint64_t epoch, uint64_t n, uint64_t mirrors, uint64_t budget, bool vram_known, uint64_t used,
                    uint64_t vram_allowed) {
    if (!budget || !busy(c, epoch)) return false;
    if (mirrors + n > ceiling(budget)) return false;
    if (!vram_known || !vram_allowed) return false;
    return used + n <= vram_allowed / 100 * kRoomPct;
}

} // namespace mirror_overflow
