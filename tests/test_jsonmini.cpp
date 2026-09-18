// Golden checks for JsonMini (Waifuland/src/JsonMini.hpp).
// Dependency-free: only the STL. Build and run with:
//   g++ -std=c++14 -Wall -Wextra -o /tmp/test_jsonmini test_jsonmini.cpp && /tmp/test_jsonmini
// Exit code 0 means every check passed; any failure prints to stderr.

#include "../src/JsonMini.hpp"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

int main()
{
    JsonMini j;

    // Basic flat extraction.
    CHECK(j.Parse("{\"command\":\"set_model\",\"index\":2}"));
    CHECK(j.GetString("command") == "set_model");
    CHECK(j.GetInt("index", -1) == 2);
    CHECK(j.WasString("command"));
    CHECK(!j.WasString("index"));

    // Absent keys yield defaults and Has() is exact.
    CHECK(!j.Has("character"));
    CHECK(j.GetInt("character", 0) == 0);
    CHECK(j.GetString("missing") == "");
    CHECK(j.GetFloat("missing", 1.5f) == 1.5f);
    CHECK(j.GetBool("missing", true) == true);

    // Substring hazards that fooled the old find-based parser: keys must
    // match exactly, and values must not leak into other keys.
    CHECK(j.Parse("{\"model_x\":1.0,\"x\":2.5}"));
    CHECK(j.GetFloat("x", 0.0f) == 2.5f);
    CHECK(j.GetFloat("model_x", 0.0f) == 1.0f);
    CHECK(!j.Has("model"));
    CHECK(!j.Has("odel_x"));

    // A value containing quote-like text must not create phantom keys.
    CHECK(j.Parse("{\"name\":\"x\",\"index\":1}"));
    CHECK(j.GetString("name") == "x");
    CHECK(j.GetInt("index", -1) == 1);
    CHECK(!j.Has("x"));

    // Values containing structural characters stay inside their string.
    CHECK(j.Parse("{\"group\":\"Tap,Body\",\"file\":\"a}b{ c.json\",\"n\":3}"));
    CHECK(j.GetString("group") == "Tap,Body");
    CHECK(j.GetString("file") == "a}b{ c.json");
    CHECK(j.GetInt("n", 0) == 3);

    // Escapes.
    CHECK(j.Parse("{\"q\":\"a\\\"b\\\\c\\n\\t\\u0041\"}"));
    CHECK(j.GetString("q") == std::string("a\"b\\c\n\tA"));

    // Numbers, negatives, booleans.
    CHECK(j.Parse("{\"a\":-1.5,\"b\":0,\"c\":true,\"d\":false,\"e\":1}"));
    CHECK(j.GetFloat("a", 0.0f) == -1.5f);
    CHECK(j.GetInt("b", 9) == 0);
    CHECK(j.GetBool("c", false) == true);
    CHECK(j.GetBool("d", true) == false);
    CHECK(j.GetBool("e", false) == true);
    CHECK(j.GetString("c") == "true");

    // String "1"/"true" also count for booleans (legacy reset payloads).
    CHECK(j.Parse("{\"reset\":\"1\"}"));
    CHECK(j.GetBool("reset", false) == true);

    // Nested object captured raw and re-parseable (set_mouth_batch shape).
    CHECK(j.Parse("{\"command\":\"set_mouth_batch\",\"mouths\":{\"0\":0.8,\"1\":0.1}}"));
    CHECK(j.GetString("command") == "set_mouth_batch");
    CHECK(j.Has("mouths"));
    CHECK(!j.WasString("mouths"));
    {
        JsonMini inner;
        CHECK(inner.Parse(j.GetString("mouths")));
        CHECK(inner.GetFloat("0", -1.0f) == 0.8f);
        CHECK(inner.GetFloat("1", -1.0f) == 0.1f);
        CHECK(!inner.Has("2"));
    }

    // String arrays.
    {
        std::vector<std::string> arr;
        CHECK(j.Parse("{\"models\":[\"A\",\"B, C\",\"D\"]}"));
        CHECK(j.GetStringArray("models", arr));
        CHECK(arr.size() == 3);
        CHECK(arr[0] == "A" && arr[1] == "B, C" && arr[2] == "D");
        CHECK(j.Parse("{\"models\":[]}"));
        CHECK(j.GetStringArray("models", arr));
        CHECK(arr.empty());
        CHECK(j.Parse("{}"));
        CHECK(!j.GetStringArray("models", arr));
    }

    // Top-level split for the config characters array.
    {
        std::vector<std::string> objs;
        CHECK(JsonMini::SplitTopLevel(
            "[{\"model\":\"A\"}, {\"model\":\"B\", \"x\":0.5}]", objs));
        CHECK(objs.size() == 2);
        JsonMini first, second;
        CHECK(first.Parse(objs[0]));
        CHECK(first.GetString("model") == "A");
        CHECK(!first.Has("x"));
        CHECK(second.Parse(objs[1]));
        CHECK(second.GetString("model") == "B");
        CHECK(second.GetFloat("x", 0.0f) == 0.5f);
    }

    // Malformed input fails cleanly instead of mis-parsing.
    CHECK(!j.Parse("not json"));
    CHECK(!j.Parse("{\"a\":}"));
    CHECK(!j.Parse(""));

    if (g_failures == 0)
        std::printf("test_jsonmini: all checks passed\n");
    else
        std::fprintf(stderr, "test_jsonmini: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
