// Golden checks for AlphaRegion (Waifuland/src/AlphaRegion.hpp).
// Dependency-free: only the STL. Build and run with:
//   g++ -std=c++14 -Wall -Wextra -o /tmp/test_alpharegion test_alpharegion.cpp && /tmp/test_alpharegion
// Exit code 0 means every check passed; any failure prints to stderr.

#include "../src/AlphaRegion.hpp"

#include <cstdio>
#include <vector>

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

using AlphaRegion::Rect;

// Builds an RGBA buffer from an ASCII picture: '#' opaque, '.' transparent,
// '+' faint (alpha 5, below the default threshold).
static std::vector<uint8_t> Mask(const char* const* rows, int w, int h)
{
    std::vector<uint8_t> buf((size_t)w * h * 4, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            buf[((size_t)y * w + x) * 4 + 3] =
                rows[y][x] == '#' ? 255 : rows[y][x] == '+' ? 5 : 0;
    return buf;
}

int main()
{
    std::vector<Rect> out;

    {
        const char* rows[] = { "....", "....", };
        auto m = Mask(rows, 4, 2);
        AlphaRegion::BuildRects(m.data(), 4, 2, 8, out);
        CHECK(out.empty());
    }

    {
        // Identical rows merge into one tall rect.
        const char* rows[] = { ".##.", ".##.", ".##.", };
        auto m = Mask(rows, 4, 3);
        AlphaRegion::BuildRects(m.data(), 4, 3, 8, out);
        CHECK(out.size() == 1);
        CHECK(out.size() == 1 && out[0] == (Rect{ 1, 0, 2, 3 }));
    }

    {
        // L shape: a tall stem band, then a wide foot band.
        const char* rows[] = { "#...", "#...", "####", };
        auto m = Mask(rows, 4, 3);
        AlphaRegion::BuildRects(m.data(), 4, 3, 8, out);
        CHECK(out.size() == 2);
        CHECK(out.size() == 2 && out[0] == (Rect{ 0, 0, 1, 2 }));
        CHECK(out.size() == 2 && out[1] == (Rect{ 0, 2, 4, 1 }));
    }

    {
        // Two spans per row (arm/body gap), a gap row, then the pattern again:
        // the gap must split bands, not merge across it.
        const char* rows[] = { "#.##", "#.##", "....", "#.##", };
        auto m = Mask(rows, 4, 4);
        AlphaRegion::BuildRects(m.data(), 4, 4, 8, out);
        CHECK(out.size() == 4);
        CHECK(out.size() == 4 && out[0] == (Rect{ 0, 0, 1, 2 }));
        CHECK(out.size() == 4 && out[1] == (Rect{ 2, 0, 2, 2 }));
        CHECK(out.size() == 4 && out[2] == (Rect{ 0, 3, 1, 1 }));
        CHECK(out.size() == 4 && out[3] == (Rect{ 2, 3, 2, 1 }));
    }

    {
        // Faint pixels (alpha 5) stay click-through at threshold 8.
        const char* rows[] = { "+#+", };
        auto m = Mask(rows, 3, 1);
        AlphaRegion::BuildRects(m.data(), 3, 1, 8, out);
        CHECK(out.size() == 1 && out[0] == (Rect{ 1, 0, 1, 1 }));
    }

    if (g_failures == 0) std::printf("ok\n");
    return g_failures == 0 ? 0 : 1;
}
