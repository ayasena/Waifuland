/**
 * @brief The pure part of subtitles: how words wrap into a block, how they
 *        move (pop in, glide, slide out), and the saved per-model settings.
 *        No GL, no clock: time and text widths are parameters, so it's
 *        tested on its own (tests/test_subtitlelayout.cpp).
 */

#pragma once

#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "JsonMini.hpp"

namespace SubtitleLayout
{

// ── Motion ──
const float kPop = 0.16f;     ///< seconds a word takes to pop in
const float kExit = 0.32f;    ///< seconds a word takes to slide out
const float kStagger = 0.025f;///< seconds between words leaving, in reading order
const float kGlide = 16.0f;   ///< ease rate of the block re-centring (1/s)
const float kHold = 1.4f;     ///< seconds the block stays after she's done
const float kIdle = 4.0f;     ///< seconds without a new word before it goes anyway
const float kSlide = 0.6f;    ///< how far left a word slides out, in ems
const int kMaxLines = 3;

/// A word popping in, `t` seconds after it's heard: 0 before, back-out
/// overshoot (~1.1) at 60%, 1 once settled. The snap of a sticker landing.
inline float PopScale(float t)
{
    if (t <= 0.0f) return 0.0f;
    float x = t / kPop;
    if (x >= 1.0f) return 1.0f;
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = x - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

/// How opaque a word popping in is: solid well before the overshoot ends.
inline float PopAlpha(float t)
{
    if (t <= 0.0f) return 0.0f;
    float a = t / (kPop * 0.35f);
    return a > 1.0f ? 1.0f : a;
}

/// A word leaving, `t` seconds after it started: 0..1, eased in (slow off
/// the mark, then away), so it reads as a deliberate slide, not a fade.
inline float ExitProgress(float t)
{
    if (t <= 0.0f) return 0.0f;
    float x = t / kExit;
    if (x >= 1.0f) return 1.0f;
    return x * x * x;
}

/// Leaving: scale 1 → 0.7 and alpha 1 → 0, together, like a sticker peeled off.
inline float ExitScale(float p) { return 1.0f - 0.3f * p; }
inline float ExitAlpha(float p) { return 1.0f - p; }

/// `now` eased toward `target` over `dt` seconds.
inline float Glide(float now, float target, float dt)
{
    return now + (target - now) * (1.0f - std::exp(-kGlide * dt));
}

/// Whether a word closes a sentence (`.`, `!`, `?`, `…`, maybe quoted).
inline bool EndsSentence(const std::string& w)
{
    size_t n = w.size();
    while (n > 0 && std::string("\"')]*~").find(w[n - 1]) != std::string::npos) n--;
    if (n == 0) return false;
    char c = w[n - 1];
    if (c == '.' || c == '!' || c == '?') return true;
    // "…" is E2 80 A6 in UTF-8.
    return n >= 3 && (unsigned char)w[n - 3] == 0xE2 && (unsigned char)w[n - 2] == 0x80 &&
           (unsigned char)w[n - 1] == 0xA6;
}

// ── Wrapping ──

/// Words of these widths, `space` apart, in lines no wider than `maxWidth`
/// (a word wider than that gets a line of its own). Indices, in order.
inline std::vector<std::vector<int>> Wrap(const std::vector<float>& widths, float space, float maxWidth)
{
    std::vector<std::vector<int>> lines;
    float x = 0.0f;
    for (int i = 0; i < (int)widths.size(); i++)
    {
        if (lines.empty() || (x > 0.0f && x + space + widths[i] > maxWidth))
        {
            lines.push_back(std::vector<int>());
            x = 0.0f;
        }
        else if (x > 0.0f)
        {
            x += space;
        }
        lines.back().push_back(i);
        x += widths[i];
    }
    return lines;
}

struct Place
{
    float x, y; ///< the word's centre, block space: x from the centre line, y down, bottom line at 0
};

/// Where each word goes: lines centred, the last line sitting on the
/// anchor, the others stacked above it.
inline std::vector<Place> Arrange(const std::vector<float>& widths, const std::vector<std::vector<int>>& lines,
                                  float space, float lineHeight)
{
    std::vector<Place> out(widths.size(), Place{ 0.0f, 0.0f });
    int n = (int)lines.size();
    for (int l = 0; l < n; l++)
    {
        float w = 0.0f;
        for (size_t k = 0; k < lines[l].size(); k++) w += widths[lines[l][k]] + (k > 0 ? space : 0.0f);
        float x = -w / 2.0f;
        float y = -(float)(n - 1 - l) * lineHeight;
        for (size_t k = 0; k < lines[l].size(); k++)
        {
            int i = lines[l][k];
            out[i].x = x + widths[i] / 2.0f;
            out[i].y = y;
            x += widths[i] + space;
        }
    }
    return out;
}

// ── Saved settings ──

struct ModelSettings
{
    bool enabled = true;
    float x = 0.0f;     ///< offset from her anchor, logical px (right)
    float y = 0.0f;     ///< offset from her anchor, logical px (down)
    float scale = 1.0f;
};

struct Settings
{
    bool enabled = true;
    std::string font;   ///< a fontconfig pattern or a file; empty = M PLUS Rounded 1c Black
    std::map<std::string, ModelSettings> models;

    ModelSettings Of(const std::string& model) const
    {
        std::map<std::string, ModelSettings>::const_iterator it = models.find(model);
        return it == models.end() ? ModelSettings() : it->second;
    }

    bool On(const std::string& model) const { return enabled && Of(model).enabled; }
};

inline float ClampScale(float s) { return s < 0.4f ? 0.4f : (s > 3.0f ? 3.0f : s); }

inline std::string Escape(const std::string& s)
{
    std::string o;
    for (char c : s)
    {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o;
}

inline std::string ModelJson(const std::string& name, const ModelSettings& m)
{
    std::ostringstream o;
    o << "{\"model\":\"" << Escape(name) << "\",\"enabled\":" << (m.enabled ? "true" : "false")
      << ",\"x\":" << m.x << ",\"y\":" << m.y << ",\"scale\":" << m.scale << "}";
    return o.str();
}

inline std::string ToJson(const Settings& s)
{
    std::ostringstream o;
    o << "{\"enabled\":" << (s.enabled ? "true" : "false") << ",\"font\":\"" << Escape(s.font)
      << "\",\"models\":[";
    bool first = true;
    for (std::map<std::string, ModelSettings>::const_iterator it = s.models.begin(); it != s.models.end(); ++it)
    {
        if (!first) o << ",";
        first = false;
        o << ModelJson(it->first, it->second);
    }
    o << "]}";
    return o.str();
}

/// Settings from their JSON; what's missing or broken keeps its default.
inline Settings FromJson(const std::string& text)
{
    Settings s;
    JsonMini j;
    if (!j.Parse(text)) return s;
    s.enabled = j.GetBool("enabled", true);
    s.font = j.GetString("font");
    std::vector<std::string> items;
    if (JsonMini::SplitTopLevel(j.GetString("models", "[]"), items))
    {
        for (size_t i = 0; i < items.size(); i++)
        {
            JsonMini m;
            if (!m.Parse(items[i])) continue;
            std::string name = m.GetString("model");
            if (name.empty()) continue;
            ModelSettings ms;
            ms.enabled = m.GetBool("enabled", true);
            ms.x = m.GetFloat("x", 0.0f);
            ms.y = m.GetFloat("y", 0.0f);
            ms.scale = ClampScale(m.GetFloat("scale", 1.0f));
            s.models[name] = ms;
        }
    }
    return s;
}

} // namespace SubtitleLayout
