#pragma once

// Alpha mask -> rectangle list, for building a Wayland input region from a
// rendered frame. Pure logic (no GL, no Wayland) so it can be tested alone.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace AlphaRegion {

struct Rect
{
    int x;
    int y;
    int width;
    int height;

    bool operator==(const Rect& o) const
    {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
};

// Turns every pixel of a tightly packed RGBA buffer (row 0 first) whose
// alpha exceeds `threshold` into rects, in the buffer's own pixel units.
// Each row becomes horizontal spans; consecutive rows with identical spans
// merge into taller rects (the y-x banding pixman uses), so a smooth
// silhouette costs about one rect per span per distinct row pattern.
inline void BuildRects(const uint8_t* rgba, int width, int height,
                       uint8_t threshold, std::vector<Rect>& out)
{
    out.clear();
    std::vector<Rect> row;       // spans of the current row (y/height unset)
    std::size_t bandStart = 0;        // first rect of the band still being extended
    std::size_t bandCount = 0;        // how many rects that band has

    for (int y = 0; y < height; y++)
    {
        row.clear();
        const uint8_t* px = rgba + (std::size_t)y * width * 4;
        int x = 0;
        while (x < width)
        {
            while (x < width && px[x * 4 + 3] <= threshold) x++;
            if (x == width) break;
            const int start = x;
            while (x < width && px[x * 4 + 3] > threshold) x++;
            row.push_back(Rect{ start, y, x - start, 1 });
        }

        bool same = row.size() == bandCount && bandCount > 0;
        for (std::size_t i = 0; same && i < bandCount; i++)
        {
            const Rect& b = out[bandStart + i];
            same = b.x == row[i].x && b.width == row[i].width &&
                   b.y + b.height == y;
        }

        if (same)
        {
            for (std::size_t i = 0; i < bandCount; i++) out[bandStart + i].height++;
        }
        else
        {
            bandStart = out.size();
            bandCount = row.size();
            out.insert(out.end(), row.begin(), row.end());
        }
    }
}

} // namespace AlphaRegion
