// Golden checks for PoseHold (Waifuland/src/PoseHold.hpp).
// Dependency-free: only the STL. Build and run with:
//   g++ -std=c++14 -Wall -Wextra -o /tmp/test_posehold test_posehold.cpp && /tmp/test_posehold
// Exit code 0 means every check passed; any failure prints to stderr.

#include "../src/PoseHold.hpp"

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

typedef std::vector<std::pair<std::string, float> > Values;

static float ValueOf(const Values& v, const std::string& part)
{
    for (size_t i = 0; i < v.size(); i++) if (v[i].first == part) return v[i].second;
    return -1.0f;
}

static PoseHold Natori()
{
    PoseHold p;
    p.groups.push_back({ "PartArmA", "PartArmB" });
    p.groups.push_back({ "PartWatchA", "PartWatchB", "PartWatchC" });
    return p;
}

static void TestNothingHeld()
{
    PoseHold p = Natori();
    CHECK(p.Values(false).empty());
    CHECK(p.Held(0).empty());
}

static void TestHold()
{
    PoseHold p = Natori();
    CHECK(p.Set(0, "PartArmB"));
    CHECK(p.Held(0) == "PartArmB");

    // Written every frame, only for the held group.
    for (int frame = 0; frame < 2; frame++)
    {
        Values v = p.Values(false);
        CHECK(v.size() == 2);
        CHECK(ValueOf(v, "PartArmA") == 0.0f);
        CHECK(ValueOf(v, "PartArmB") == 1.0f);
    }

    // Changed by another set.
    CHECK(p.Set(0, "PartArmA"));
    CHECK(ValueOf(p.Values(false), "PartArmA") == 1.0f);
}

static void TestUnknown()
{
    PoseHold p = Natori();
    CHECK(!p.Set(0, "PartWatchA"));  // in the pose, not in this group
    CHECK(!p.Set(2, "PartArmA"));
    CHECK(!p.Set(-1, "PartArmA"));
    CHECK(!p.Release(5));
    CHECK(p.Release(1));             // nothing held: fine, nothing to put back
    CHECK(p.Values(false).empty());
}

static void TestMotionWins()
{
    PoseHold p = Natori();
    p.Set(1, "PartWatchC");
    CHECK(p.Values(true).empty());   // the motion drives the parts
    Values after = p.Values(false);  // and the held part comes back after
    CHECK(ValueOf(after, "PartWatchC") == 1.0f);
    CHECK(ValueOf(after, "PartWatchA") == 0.0f);
}

static void TestRelease()
{
    PoseHold p = Natori();
    p.Set(0, "PartArmB");
    p.Set(1, "PartWatchB");
    CHECK(p.Release(0));
    CHECK(p.Held(0).empty());

    // The default part once (waiting out a playing motion), then only the held group.
    CHECK(p.Values(true).empty());
    Values v = p.Values(false);
    CHECK(ValueOf(v, "PartArmA") == 1.0f);
    CHECK(ValueOf(v, "PartArmB") == 0.0f);
    CHECK(ValueOf(v, "PartWatchB") == 1.0f);
    v = p.Values(false);
    CHECK(v.size() == 3);
    CHECK(ValueOf(v, "PartArmA") == -1.0f);

    // Holding again after a release cancels the pending default.
    p.Set(1, "PartWatchA");
    p.Release(1);
    p.Set(1, "PartWatchC");
    v = p.Values(false);
    CHECK(v.size() == 3);
    CHECK(ValueOf(v, "PartWatchC") == 1.0f);
}

int main()
{
    TestNothingHeld();
    TestHold();
    TestUnknown();
    TestMotionWins();
    TestRelease();
    if (g_failures == 0) std::printf("test_posehold: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
