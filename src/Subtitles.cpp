#include <GL/glew.h>

#include "Subtitles.hpp"

#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "LAppConfig.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppPal.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

using namespace SubtitleLayout;

namespace {

// ── Look ──
const float kFontPx = 40.0f;   ///< em of a subtitle at scale 1, logical px
const float kMaxEms = 14.0f;   ///< a line is at most this many ems wide
const float kSpace = 0.42f;    ///< between words, ems
const float kTracking = 0.05f; ///< between letters, ems: room for their keylines
const float kLineHeight = 1.34f;
/// The lettering unless the settings name another (a fontconfig pattern or
/// a file): heavy and round, and it has Japanese too.
const char* kDefaultFont = "Rounded Mplus 1c:style=Black";
const float kKeyline = 0.16f;  ///< the black edge, ems: a sticker's, not a stroke
/// Her anchor: this far down her box (models often run off its bottom).
const float kAnchorDown = 0.86f;
const char* kSample[] = { "Subtitles", "go", "here!" };

// ── Glyphs: signed distance fields in one atlas ──
const float kEm = 64.0f;       ///< atlas em, px
const int kPad = 12;           ///< distance field reach around a glyph, px
const float kDistScale = 128.0f / kPad;
const int kAtlas = 1024;

double Now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct Font
{
    std::vector<unsigned char> data;
    stbtt_fontinfo info;
    float scale = 0.0f;  ///< font units → kEm px
    float ascent = 0.0f; ///< kEm px
    float descent = 0.0f;
    bool ok = false;

    bool Load(const std::string& path)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in) return false;
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (data.empty() || !stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)))
            return false;
        scale = stbtt_ScaleForMappingEmToPixels(&info, kEm);
        int a, d, g;
        stbtt_GetFontVMetrics(&info, &a, &d, &g);
        ascent = a * scale;
        descent = d * scale;
        ok = true;
        return true;
    }
};

struct Glyph
{
    bool drawn = false;           ///< has a quad (a space doesn't)
    float u0, v0, u1, v1;
    float x0, y0, x1, y1;         ///< quad from the pen, kEm px, y down from the baseline
    float advance = 0.0f;         ///< kEm px
};

Font g_main, g_fallback;
std::map<int, Glyph> g_glyphs;
bool g_glTried = false, g_glOk = false;
GLuint g_tex = 0, g_prog = 0, g_vbo = 0;
GLint g_aPos = -1, g_aUv = -1, g_aAlpha = -1, g_uSize = -1, g_uTex = -1, g_uKeyline = -1;
int g_penX = 1, g_penY = 1, g_rowH = 0;

/// A font file fontconfig has for `pattern`, and whether its family matches.
std::string FcMatch(const std::string& pattern, const std::string& family)
{
    std::string cmd = "fc-match -f '%{family}\\n%{file}' '" + pattern + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    std::string out;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    size_t nl = out.find('\n');
    if (nl == std::string::npos) return "";
    std::string fam = out.substr(0, nl), file = out.substr(nl + 1);
    if (!family.empty() && fam.find(family) == std::string::npos) return "";
    return file;
}

void LoadFonts(const std::string& configured)
{
    if (g_main.ok) return;
    // A file (has a slash, or ~), else a fontconfig pattern whose family
    // must match: fc-match always answers, with some sans if it has to.
    std::string want = configured.empty() ? kDefaultFont : configured;
    std::string path;
    if (want.find('/') != std::string::npos || want[0] == '~')
        path = LAppConfig::ExpandTilde(want);
    else
        path = FcMatch(want, want.substr(0, want.find(':')));
    if (path.empty() || !g_main.Load(path))
    {
        LAppPal::PrintLogLn("[Subtitles] font %s not found (sucrette install --only fonts); using the default sans",
                            want.c_str());
        g_main.Load(FcMatch("sans-serif:bold", ""));
    }
    else
    {
        LAppPal::PrintLogLn("[Subtitles] font: %s", path.c_str());
    }
    // What the main font lacks (other scripts) comes from here.
    g_fallback.Load(FcMatch("sans-serif:bold", ""));
}

/// Forget the fonts and every glyph drawn with them (another font asked for).
void ResetFonts()
{
    g_main = Font();
    g_fallback = Font();
    g_glyphs.clear();
    g_penX = g_penY = 1;
    g_rowH = 0;
}

GLuint Compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LAppPal::PrintLogLn("[Subtitles] shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

const char* kVertex = R"(#version 120
attribute vec2 a_pos;
attribute vec2 a_uv;
attribute float a_alpha;
uniform vec2 u_size;
varying vec2 v_uv;
varying float v_alpha;
void main() {
    v_uv = a_uv;
    v_alpha = a_alpha;
    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);
}
)";

// White fill, black keyline, both edges smoothed by the field itself, so the
// shape stays crisp at any size; out premultiplied, as the compositor wants.
const char* kFragment = R"(#version 120
uniform sampler2D u_tex;
uniform float u_keyline;
varying vec2 v_uv;
varying float v_alpha;
void main() {
    float d = texture2D(u_tex, v_uv).a;
    float w = max(fwidth(d) * 0.7, 0.002);
    float fill = smoothstep(0.5 - w, 0.5 + w, d);
    float shape = smoothstep(u_keyline - w, u_keyline + w, d);
    float a = shape * v_alpha;
    gl_FragColor = vec4(vec3(fill) * a, a);
}
)";

int Utf8Next(const std::string& s, size_t& i)
{
    unsigned char c = s[i++];
    int cp, extra;
    if (c < 0x80) return c;
    else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
    else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
    else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
    else return 0xFFFD;
    while (extra-- > 0 && i < s.size()) cp = (cp << 6) | (s[i++] & 0x3F);
    return cp;
}

} // namespace

Subtitles& Subtitles::Get()
{
    static Subtitles s;
    return s;
}

Subtitles::Subtitles()
{
    Load();
}

static std::string StatePath()
{
    const char* state = std::getenv("XDG_STATE_HOME");
    std::string base = (state && *state) ? state : std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.local/state";
    return base + "/waifuland/subtitles.json";
}

void Subtitles::Load()
{
    std::ifstream in(StatePath().c_str());
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    _settings = FromJson(ss.str());
}

void Subtitles::Save()
{
    _saveAt = -1.0;
    std::string path = StatePath();
    std::string dir = path.substr(0, path.rfind('/'));
    mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp.c_str(), std::ios::trunc);
        if (!out) return;
        out << ToJson(_settings) << "\n";
    }
    std::rename(tmp.c_str(), path.c_str());
}

bool Subtitles::EnsureGl()
{
    if (g_glTried) return g_glOk;
    g_glTried = true;
    LoadFonts(_settings.font);
    if (!g_main.ok) return false;
    GLuint vs = Compile(GL_VERTEX_SHADER, kVertex), fs = Compile(GL_FRAGMENT_SHADER, kFragment);
    if (!vs || !fs) return false;
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    g_aPos = glGetAttribLocation(g_prog, "a_pos");
    g_aUv = glGetAttribLocation(g_prog, "a_uv");
    g_aAlpha = glGetAttribLocation(g_prog, "a_alpha");
    g_uSize = glGetUniformLocation(g_prog, "u_size");
    g_uTex = glGetUniformLocation(g_prog, "u_tex");
    g_uKeyline = glGetUniformLocation(g_prog, "u_keyline");

    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    std::vector<unsigned char> zero(kAtlas * kAtlas, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kAtlas, kAtlas, 0, GL_ALPHA, GL_UNSIGNED_BYTE, zero.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    glGenBuffers(1, &g_vbo);
    g_glOk = true;
    return true;
}

/// The glyph for `cp`, rendered into the atlas the first time it's asked for.
static const Glyph& GlyphOf(int cp)
{
    std::map<int, Glyph>::iterator it = g_glyphs.find(cp);
    if (it != g_glyphs.end()) return it->second;
    Glyph g;
    Font* f = &g_main;
    int index = stbtt_FindGlyphIndex(&f->info, cp);
    if (index == 0 && g_fallback.ok && stbtt_FindGlyphIndex(&g_fallback.info, cp) != 0)
    {
        f = &g_fallback;
        index = stbtt_FindGlyphIndex(&f->info, cp);
    }
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f->info, index, &adv, &lsb);
    // The fallback is matched to the main font's size by its em.
    g.advance = adv * f->scale;
    int w = 0, h = 0, xo = 0, yo = 0;
    unsigned char* sdf = stbtt_GetGlyphSDF(&f->info, f->scale, index, kPad, 128, kDistScale, &w, &h, &xo, &yo);
    if (sdf && w > 0 && h > 0)
    {
        if (g_penX + w + 1 > kAtlas)
        {
            g_penX = 1;
            g_penY += g_rowH + 1;
            g_rowH = 0;
        }
        if (g_penY + h + 1 > kAtlas)
        {
            // Full (a lot of scripts): start over; what's shown re-renders.
            LAppPal::PrintLogLn("[Subtitles] glyph atlas full; starting it over");
            g_glyphs.clear();
            g_penX = g_penY = 1;
            g_rowH = 0;
        }
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, g_penX, g_penY, w, h, GL_ALPHA, GL_UNSIGNED_BYTE, sdf);
        glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
        g.drawn = true;
        g.u0 = (float)g_penX / kAtlas;
        g.v0 = (float)g_penY / kAtlas;
        g.u1 = (float)(g_penX + w) / kAtlas;
        g.v1 = (float)(g_penY + h) / kAtlas;
        g.x0 = (float)xo;
        g.y0 = (float)yo;
        g.x1 = (float)(xo + w);
        g.y1 = (float)(yo + h);
        g_penX += w + 1;
        if (h > g_rowH) g_rowH = h;
    }
    if (sdf) stbtt_FreeSDF(sdf, NULL);
    return g_glyphs[cp] = g;
}

float Subtitles::Measure(const std::string& text)
{
    if (!EnsureGl()) return 0.0f;
    float x = 0.0f;
    int n = 0;
    for (size_t i = 0; i < text.size(); n++) x += GlyphOf(Utf8Next(text, i)).advance;
    // Tracking goes between letters, not after the last.
    return x / kEm + kTracking * (n > 0 ? n - 1 : 0);
}

std::string Subtitles::ModelOf(int character) const
{
    LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
    Csm::csmInt32 idx = mgr->GetCharacterModelDirIndex(character);
    const Csm::csmVector<Csm::csmString>& dirs = mgr->GetModelDir();
    return (idx >= 0 && idx < (Csm::csmInt32)dirs.GetSize()) ? dirs[idx].GetRawString() : "";
}

void Subtitles::Leave(Block& b, double now, size_t from, size_t to)
{
    int k = 0;
    for (size_t i = from; i < to && i < b.words.size(); i++)
    {
        if (b.words[i].leaving >= 0.0) continue;
        b.words[i].leaving = now + (k++) * kStagger;
    }
}

void Subtitles::Word(int character, const std::string& text, int dueMs, const std::string& utterance)
{
    if (text.empty() || !_settings.On(ModelOf(character))) return;
    double now = Now();
    Block& b = _blocks[character];
    if (b.closed || b.utterance != utterance)
    {
        // A new sentence, or someone new talking: the old block goes.
        Leave(b, now, 0, b.words.size());
        b.closed = false;
    }
    b.utterance = utterance;
    Said w;
    w.text = text;
    w.due = now + (dueMs > 0 ? dueMs : 0) / 1000.0;
    w.width = Measure(text);
    b.words.push_back(w);
    if (w.due > b.lastDue) b.lastDue = w.due;
    b.goAt = -1.0;
    b.closed = EndsSentence(text);
}

void Subtitles::End(int character, const std::string& utterance)
{
    std::map<int, Block>::iterator it = _blocks.find(character);
    if (it == _blocks.end() || it->second.utterance != utterance) return;
    double now = Now();
    it->second.goAt = (it->second.lastDue > now ? it->second.lastDue : now) + kHold;
}

void Subtitles::Preview(float seconds)
{
    double now = Now();
    _previewUntil = now + seconds;
    LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
    for (Csm::csmInt32 i = 0; i < mgr->GetCharacterCount(); i++)
    {
        int id = mgr->GetCharacterIdAt(i);
        Block& b = _blocks[id];
        bool speaking = false;
        for (size_t k = 0; k < b.words.size(); k++) speaking |= b.words[k].leaving < 0.0;
        if (speaking) continue;
        for (int k = 0; k < 3; k++) Word(id, kSample[k], 120 * k, "preview");
        b.goAt = now + seconds;
    }
}

void Subtitles::Clear()
{
    _blocks.clear();
    _boxes.clear();
}

void Subtitles::Layout(Block& b, float em, float dt, double now)
{
    (void)em;
    for (int pass = 0; pass < 2; pass++)
    {
        std::vector<int> shown;
        std::vector<float> widths;
        for (size_t i = 0; i < b.words.size(); i++)
        {
            if (b.words[i].leaving >= 0.0 || b.words[i].due > now) continue;
            shown.push_back((int)i);
            widths.push_back(b.words[i].width);
        }
        std::vector<std::vector<int>> lines = Wrap(widths, kSpace, kMaxEms);
        if (pass == 0 && (int)lines.size() > kMaxLines)
        {
            // Too tall: the top lines' words go, the rest glide up.
            int k = 0;
            for (int l = 0; l < (int)lines.size() - kMaxLines; l++)
            {
                for (size_t j = 0; j < lines[l].size(); j++)
                    b.words[shown[lines[l][j]]].leaving = now + (k++) * kStagger;
            }
            continue;
        }
        std::vector<Place> places = Arrange(widths, lines, kSpace, kLineHeight);
        for (size_t j = 0; j < shown.size(); j++)
        {
            Said& w = b.words[shown[j]];
            if (!w.placed)
            {
                w.x = places[j].x;
                w.y = places[j].y;
                w.placed = true;
            }
            else
            {
                w.x = Glide(w.x, places[j].x, dt);
                w.y = Glide(w.y, places[j].y, dt);
            }
        }
        break;
    }
}

int Subtitles::HitTest(float x, float y) const
{
    for (size_t i = 0; i < _boxes.size(); i++)
    {
        const Box& b = _boxes[i];
        if (x >= b.left && x < b.left + b.width && y >= b.top && y < b.top + b.height) return b.character;
    }
    return -1;
}

void Subtitles::Move(int character, float dx, float dy)
{
    std::string m = ModelOf(character);
    if (m.empty()) return;
    ModelSettings s = _settings.Of(m);
    s.x += dx;
    s.y += dy;
    _settings.models[m] = s;
}

void Subtitles::Scale(int character, float factor)
{
    std::string m = ModelOf(character);
    if (m.empty()) return;
    ModelSettings s = _settings.Of(m);
    s.scale = ClampScale(s.scale * factor);
    _settings.models[m] = s;
    _saveAt = Now() + 1.0; // once the scrolling stops
}

void Subtitles::SetFont(const std::string& font)
{
    _settings.font = font;
    ResetFonts();
    Clear(); // what's shown was measured in the old font
    if (g_glTried && g_glOk) LoadFonts(_settings.font);
    Save();
}

void Subtitles::SetEnabled(bool on)
{
    _settings.enabled = on;
    if (!on) Clear();
    Save();
}

void Subtitles::SetModel(const std::string& model, const ModelSettings& m)
{
    ModelSettings s = m;
    s.scale = ClampScale(s.scale);
    _settings.models[model] = s;
    if (!s.enabled)
    {
        for (std::map<int, Block>::iterator it = _blocks.begin(); it != _blocks.end();)
        {
            if (ModelOf(it->first) == model) it = _blocks.erase(it);
            else ++it;
        }
    }
    Save();
}

std::string Subtitles::Json() const
{
    std::ostringstream o;
    std::string all = ToJson(_settings);
    o << all.substr(0, all.size() - 1) << ",\"characters\":[";
    LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
    for (Csm::csmInt32 i = 0; i < mgr->GetCharacterCount(); i++)
    {
        int id = mgr->GetCharacterIdAt(i);
        std::string m = ModelOf(id);
        std::string one = ModelJson(m, _settings.Of(m));
        if (i > 0) o << ",";
        o << "{\"id\":" << id << "," << one.substr(1);
    }
    // Where they're drawn now: what a drag or a scroll has to land on.
    o << "],\"boxes\":[";
    for (size_t i = 0; i < _boxes.size(); i++)
    {
        const Box& b = _boxes[i];
        o << (i ? "," : "") << "{\"character\":" << b.character << ",\"left\":" << b.left << ",\"top\":" << b.top
          << ",\"width\":" << b.width << ",\"height\":" << b.height << "}";
    }
    o << "]}";
    return o.str();
}

void Subtitles::Frame(int width, int height, float pixelScale)
{
    (void)pixelScale;
    double now = Now();
    float dt = _lastFrame > 0.0 ? (float)(now - _lastFrame) : 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    _lastFrame = now;
    _boxes.clear();
    if (_saveAt >= 0.0 && now >= _saveAt) Save();
    if (_blocks.empty() || !_settings.enabled || width <= 0 || height <= 0) return;
    if (!EnsureGl()) return;

    Csm::csmVector<LAppLive2DManager::ScreenRect> rects;
    LAppLive2DManager::GetInstance()->GetCharacterPixelRects(width, height, rects);

    struct Vertex { float x, y, u, v, a; };
    std::vector<Vertex> verts;

    for (std::map<int, Block>::iterator it = _blocks.begin(); it != _blocks.end();)
    {
        Block& b = it->second;
        const LAppLive2DManager::ScreenRect* rect = NULL;
        for (Csm::csmUint32 r = 0; r < rects.GetSize(); r++)
            if (rects[r].id == it->first) rect = &rects[r];
        std::string model = ModelOf(it->first);
        if (rect == NULL || !_settings.On(model))
        {
            it = _blocks.erase(it);
            continue;
        }

        // When she's done (or quiet too long), everything left goes.
        bool staying = false;
        for (size_t i = 0; i < b.words.size(); i++) staying |= b.words[i].leaving < 0.0;
        if (staying && ((b.goAt >= 0.0 && now >= b.goAt) || now > b.lastDue + kIdle))
            Leave(b, now, 0, b.words.size());

        ModelSettings ms = _settings.Of(model);
        float em = kFontPx * ms.scale;
        Layout(b, em, dt, now);

        // Gone words are dropped; a block with none left is too.
        for (size_t i = 0; i < b.words.size();)
        {
            if (b.words[i].leaving >= 0.0 && now - b.words[i].leaving >= kExit) b.words.erase(b.words.begin() + i);
            else i++;
        }
        if (b.words.empty())
        {
            it = _blocks.erase(it);
            continue;
        }

        float ax = rect->left + rect->width * 0.5f + ms.x;
        float ay = rect->top + rect->height * kAnchorDown + ms.y;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        float px = em / kEm;
        // The glyph's box sits on its baseline; centre it on the line.
        float mid = (g_main.ascent + g_main.descent) * 0.5f;
        for (size_t i = 0; i < b.words.size(); i++)
        {
            const Said& w = b.words[i];
            if (!w.placed) continue;
            float t = (float)(now - w.due);
            float scale = PopScale(t), alpha = PopAlpha(t), slide = 0.0f;
            if (w.leaving >= 0.0)
            {
                float p = ExitProgress((float)(now - w.leaving));
                scale = ExitScale(p);
                alpha = ExitAlpha(p);
                slide = -kSlide * p;
            }
            if (scale <= 0.0f || alpha <= 0.0f) continue;
            float cx = ax + (w.x + slide) * em, cy = ay + w.y * em;
            float pen = -w.width * kEm * 0.5f;
            for (size_t k = 0; k < w.text.size();)
            {
                const Glyph& g = GlyphOf(Utf8Next(w.text, k));
                if (g.drawn)
                {
                    float x0 = cx + (pen + g.x0) * px * scale, x1 = cx + (pen + g.x1) * px * scale;
                    float y0 = cy + (g.y0 - mid) * px * scale, y1 = cy + (g.y1 - mid) * px * scale;
                    Vertex q[6] = {
                        { x0, y0, g.u0, g.v0, alpha }, { x1, y0, g.u1, g.v0, alpha }, { x1, y1, g.u1, g.v1, alpha },
                        { x0, y0, g.u0, g.v0, alpha }, { x1, y1, g.u1, g.v1, alpha }, { x0, y1, g.u0, g.v1, alpha },
                    };
                    verts.insert(verts.end(), q, q + 6);
                    if (w.leaving < 0.0)
                    {
                        minX = std::fmin(minX, x0); maxX = std::fmax(maxX, x1);
                        minY = std::fmin(minY, y0); maxY = std::fmax(maxY, y1);
                    }
                }
                pen += g.advance + kTracking * kEm;
            }
        }
        if (maxX > minX)
        {
            const float pad = 10.0f; // a little forgiving to grab
            Box box = { it->first, (int)(minX - pad), (int)(minY - pad), (int)(maxX - minX + 2 * pad),
                        (int)(maxY - minY + 2 * pad) };
            _boxes.push_back(box);
        }
        ++it;
    }
    if (verts.empty()) return;

    // Draw, leaving GL as the renderer had it.
    GLint prog = 0, buf = 0, tex = 0, active = 0, src = 0, dst = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glGetIntegerv(GL_BLEND_SRC_RGB, &src);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst);
    GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST),
              depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE),
              stencil = glIsEnabled(GL_STENCIL_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glUseProgram(g_prog);
    glUniform2f(g_uSize, (float)width, (float)height);
    glUniform1i(g_uTex, 0);
    glUniform1f(g_uKeyline, 0.5f - kKeyline * kEm * kDistScale / 255.0f);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)), verts.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(g_aPos);
    glEnableVertexAttribArray(g_aUv);
    glEnableVertexAttribArray(g_aAlpha);
    glVertexAttribPointer(g_aPos, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glVertexAttribPointer(g_aUv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glVertexAttribPointer(g_aAlpha, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(4 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glDisableVertexAttribArray(g_aPos);
    glDisableVertexAttribArray(g_aUv);
    glDisableVertexAttribArray(g_aAlpha);

    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)buf);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
    glActiveTexture((GLenum)active);
    glUseProgram((GLuint)prog);
    glBlendFunc((GLenum)src, (GLenum)dst);
    if (!blend) glDisable(GL_BLEND);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (stencil) glEnable(GL_STENCIL_TEST);
}
