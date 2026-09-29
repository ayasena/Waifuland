// Golden checks for SubtitleLayout (Waifuland/src/SubtitleLayout.hpp).
// Dependency-free: only the STL. Build and run with:
//   g++ -std=c++14 -Wall -Wextra -o /tmp/test_subtitlelayout test_subtitlelayout.cpp && /tmp/test_subtitlelayout
// Exit code 0 means every check passed; any failure prints to stderr.

#include "../src/SubtitleLayout.hpp"

#include <cmath>
#include <cstdio>

using namespace SubtitleLayout;

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void TestWrap()
{
    // 10-wide words, 2 apart, lines of at most 34: three to a line.
    std::vector<float> w(7, 10.0f);
    std::vector<std::vector<int>> lines = Wrap(w, 2.0f, 34.0f);
    CHECK(lines.size() == 3);
    CHECK(lines[0].size() == 3 && lines[1].size() == 3 && lines[2].size() == 1);
    CHECK(lines[2][0] == 6);

    // A word wider than the line still gets one, alone.
    std::vector<float> wide = { 5.0f, 100.0f, 5.0f };
    lines = Wrap(wide, 1.0f, 50.0f);
    CHECK(lines.size() == 3);

    CHECK(Wrap(std::vector<float>(), 1.0f, 10.0f).empty());
}

static void TestArrange()
{
    std::vector<float> w = { 10.0f, 20.0f, 10.0f };
    // Two lines: [0 1] then [2].
    std::vector<std::vector<int>> lines = { { 0, 1 }, { 2 } };
    std::vector<Place> p = Arrange(w, lines, 2.0f, 30.0f);
    // First line is 32 wide, centred: word 0 spans -16..-6.
    CHECK(Near(p[0].x, -11.0f));
    CHECK(Near(p[1].x, 6.0f));
    CHECK(Near(p[0].y, -30.0f)); // stacked above the last line
    CHECK(Near(p[2].x, 0.0f));   // alone, centred
    CHECK(Near(p[2].y, 0.0f));   // the last line sits on the anchor
}

static void TestMotion()
{
    CHECK(PopScale(-0.1f) == 0.0f);
    CHECK(PopScale(0.0f) == 0.0f);
    CHECK(PopScale(kPop) == 1.0f);
    CHECK(PopScale(10.0f) == 1.0f);
    // It overshoots, then settles: a snap, not a bloom.
    float peak = 0.0f;
    for (int i = 1; i < 100; i++) peak = std::fmax(peak, PopScale(kPop * i / 100.0f));
    CHECK(peak > 1.05f && peak < 1.2f);
    CHECK(PopAlpha(kPop) == 1.0f);

    CHECK(ExitProgress(0.0f) == 0.0f);
    CHECK(ExitProgress(kExit) == 1.0f);
    CHECK(ExitProgress(kExit / 2.0f) < 0.5f); // eased in: slow off the mark
    CHECK(Near(ExitScale(1.0f), 0.7f));
    CHECK(ExitAlpha(1.0f) == 0.0f);

    CHECK(Near(Glide(0.0f, 10.0f, 10.0f), 10.0f));
    float g = Glide(0.0f, 10.0f, 1.0f / 60.0f);
    CHECK(g > 0.0f && g < 10.0f);
}

static void TestSentences()
{
    CHECK(EndsSentence("done."));
    CHECK(EndsSentence("really?!"));
    CHECK(EndsSentence("\"wow!\""));
    CHECK(EndsSentence("so\xE2\x80\xA6"));
    CHECK(!EndsSentence("well,"));
    CHECK(!EndsSentence("e.g"));
    CHECK(!EndsSentence(""));
}

static void TestSettings()
{
    Settings s;
    s.enabled = false;
    s.font = "/fonts/Some \"Font\".ttf";
    ModelSettings haru;
    haru.enabled = false;
    haru.x = 12.5f;
    haru.y = -40.0f;
    haru.scale = 1.5f;
    s.models["Haru"] = haru;
    s.models["Hiyori"] = ModelSettings();

    Settings back = FromJson(ToJson(s));
    CHECK(!back.enabled);
    CHECK(back.font == s.font);
    CHECK(back.models.size() == 2);
    CHECK(!back.Of("Haru").enabled);
    CHECK(Near(back.Of("Haru").x, 12.5f) && Near(back.Of("Haru").y, -40.0f));
    CHECK(Near(back.Of("Haru").scale, 1.5f));
    CHECK(back.Of("Hiyori").enabled);

    // Unknown models get the defaults; the global switch wins.
    CHECK(back.Of("Mao").enabled && back.Of("Mao").scale == 1.0f);
    CHECK(!back.On("Hiyori"));

    // Colours round-trip; a bad one keeps the default.
    s.color = "#ffe066";
    s.outline = "202040";
    Settings c = FromJson(ToJson(s));
    CHECK(c.color == "#ffe066" && c.outline == "202040");
    CHECK(FromJson("{\"color\":\"red\"}").color == "#ffffff");
    float rgb[3];
    CHECK(ParseHex("#ff0000", rgb) && Near(rgb[0], 1.0f) && Near(rgb[1], 0.0f));
    CHECK(!ParseHex("#ff00", rgb) && !ParseHex("#gg0000", rgb));

    // A broken file keeps the defaults; a wild scale is clamped.
    CHECK(FromJson("not json").enabled);
    CHECK(Near(FromJson("{\"models\":[{\"model\":\"A\",\"scale\":99}]}").Of("A").scale, 3.0f));
}

int main()
{
    TestWrap();
    TestArrange();
    TestMotion();
    TestSentences();
    TestSettings();
    if (g_failures == 0) std::printf("test_subtitlelayout: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
