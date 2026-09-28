/**
 * @brief Each character's subtitles, drawn in the overlay beside her: what
 *        she says, word by word as it's heard, in heavy rounded lettering
 *        (M PLUS Rounded 1c Black by default) with a black keyline. Words pop in, the
 *        block re-centres as it grows, and words slide out to the left once
 *        she's done. Dragged and scrolled like a character (move, scale);
 *        on or off per model and globally, saved in
 *        $XDG_STATE_HOME/waifuland/subtitles.json.
 *
 *        Fed over IPC (`subtitle_word`, `subtitle_end`); the motion and
 *        wrapping are SubtitleLayout.hpp.
 */

#pragma once

#include <map>
#include <string>
#include <vector>

#include "SubtitleLayout.hpp"

class Subtitles
{
public:
    static Subtitles& Get();

    /// A word `character` will say in `dueMs`, as part of `utterance`.
    void Word(int character, const std::string& text, int dueMs, const std::string& utterance);
    /// `character` is done saying `utterance`: the block goes after a moment.
    void End(int character, const std::string& utterance);
    /// A sample line on every character for `seconds`, to arrange them.
    void Preview(float seconds);
    /// Everything gone at once (the stage hidden).
    void Clear();

    /// Move on and draw, after the characters and before the input region
    /// is read back (so drawn subtitles take the mouse, as characters do).
    /// Window size in logical pixels; the framebuffer is `pixelScale` times it.
    void Frame(int width, int height, float pixelScale);

    struct Box { int character; int left, top, width, height; };
    /// Where subtitles are drawn now (logical px, top-left origin).
    const std::vector<Box>& Boxes() const { return _boxes; }
    /// The character whose subtitle is at (x, y), or -1.
    int HitTest(float x, float y) const;
    /// Drag `character`'s subtitle by (dx, dy) logical px; scale it by `factor`.
    void Move(int character, float dx, float dy);
    void Scale(int character, float factor);
    /// A drag is over: keep where it is.
    void Save();

    const SubtitleLayout::Settings& Settings() const { return _settings; }
    void SetEnabled(bool on);
    /// Letter with `font` (a fontconfig pattern or a file; empty: the default).
    void SetFont(const std::string& font);
    void SetModel(const std::string& model, const SubtitleLayout::ModelSettings& m);
    /// The settings and the characters shown, as JSON (`get_subtitles`).
    std::string Json() const;

private:
    Subtitles();

    /// A word in a block.
    struct Said
    {
        std::string text;
        double due;        ///< steady seconds it's heard
        float width;       ///< at 1 em
        float x, y;        ///< centre, block space, eased toward the layout
        bool placed = false;
        double leaving = -1.0; ///< steady seconds it starts to slide out; < 0: staying
    };

    struct Block
    {
        std::vector<Said> words;   ///< staying (in order), then leaving
        std::string utterance;
        bool closed = false;       ///< the last word ended a sentence
        double goAt = -1.0;        ///< when the rest leaves (< 0: not yet known)
        double lastDue = 0.0;
    };

    void Leave(Block& b, double now, size_t from, size_t to);
    void Layout(Block& b, float em, float dt, double now);
    std::string ModelOf(int character) const;
    void Load();
    bool EnsureGl();
    float Measure(const std::string& text);

    SubtitleLayout::Settings _settings;
    std::map<int, Block> _blocks;
    std::vector<Box> _boxes;
    double _lastFrame = 0.0;
    double _saveAt = -1.0;
    double _previewUntil = -1.0;
};
