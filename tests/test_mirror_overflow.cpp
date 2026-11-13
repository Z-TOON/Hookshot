#include "../gpu/vk/mirror_overflow.h"

#include <cstdio>

#define CHECK(name, condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s at line %d\n", name, __LINE__); return 1; } } while (0)

namespace mo = mirror_overflow;

int main() {
    const uint64_t MB = 1ull << 20;
    // a pool bound in every submission is busy after kHot of them, not before
    {
        mo::Count c;
        for (uint64_t e = 1000; e < 1000 + mo::kHot - 1; e++) mo::note(c, e);
        CHECK("not yet", !mo::busy(c, 1000 + mo::kHot - 2));
        mo::note(c, 1000 + mo::kHot - 1);
        CHECK("busy", mo::busy(c, 1000 + mo::kHot - 1) && c.n == mo::kHot);
    }
    // counted once a submission, however often it is asked in it
    {
        mo::Count c;
        for (int i = 0; i < 500; i++) mo::note(c, 7);
        CHECK("once a submission", c.n == 1 && !mo::busy(c, 7));
    }
    // bound in one submission of three: never busy, and its window starts over
    {
        mo::Count c;
        bool ever = false;
        for (uint64_t e = 1; e < 20000; e += 3) mo::note(c, e), ever |= mo::busy(c, e);
        CHECK("now and then", !ever);
        CHECK("window restarts", c.n <= mo::kWindow / 3 + 2);
    }
    // bound in two submissions of three: busy
    {
        mo::Count c;
        bool ever = false;
        for (uint64_t e = 1; e < 600; e++)
            if (e % 3) mo::note(c, e), ever |= mo::busy(c, e);
        CHECK("most submissions", ever);
    }
    // the measured case: an RTX 2080, 152 MB refused with 1016 of 1024 MB of mirrors, the driver's count 6065 of 7169 MB
    {
        mo::Count c;
        for (uint64_t e = 1; e <= mo::kHot; e++) mo::note(c, e);
        const uint64_t e = mo::kHot;
        CHECK("the measured case", mo::allowed(c, e, 152 * MB, 1016 * MB, 1024 * MB, true, 6065 * MB, 7169 * MB));
        // the card nearly full: not past kRoomPct
        CHECK("no room on the card", !mo::allowed(c, e, 152 * MB, 1016 * MB, 1024 * MB, true, 6800 * MB, 7169 * MB));
        // the same card with 0.8 GB of it taken by other programs, the count at 91%: still made
        CHECK("a card shared with the desktop", mo::allowed(c, e, 152 * MB, 1016 * MB, 1024 * MB, true, 5753 * MB, 6329 * MB));
        // the driver's count not known: refused
        CHECK("count unknown", !mo::allowed(c, e, 152 * MB, 1016 * MB, 1024 * MB, false, 0, 0));
        // past half the budget again in all: refused
        CHECK("the ceiling", !mo::allowed(c, e, 152 * MB, 1400 * MB, 1024 * MB, true, 6065 * MB, 7169 * MB));
        CHECK("at the ceiling", mo::allowed(c, e, 152 * MB, 1384 * MB, 1024 * MB, true, 6065 * MB, 7169 * MB));
        // mirrors off (no budget): nothing
        CHECK("no budget", !mo::allowed(c, e, 152 * MB, 0, 0, true, 6065 * MB, 7169 * MB));
        // a pool that is not busy: refused whatever the room
        mo::Count idle;
        mo::note(idle, 5);
        CHECK("not busy", !mo::allowed(idle, 5, 1 * MB, 0, 1024 * MB, true, 0, 7169 * MB));
    }
    CHECK("ceiling", mo::ceiling(1024 * MB) == 1536 * MB);
    std::printf("PASS: a busy pool refused by the mirrors' budget is made past it while the card has room\n");
    return 0;
}
