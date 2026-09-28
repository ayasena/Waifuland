#include <GL/glew.h>
#include "LAppWaylandRegion.hpp"
#include "LAppWayland.hpp"
#include "LAppLive2DManager.hpp"
#include "AlphaRegion.hpp"
#include "Subtitles.hpp"
#include <Type/csmVector.hpp>
#include <cstring>
#include <vector>

// The input region is built from the frame that was actually drawn, so only
// painted pixels (texture alpha, clipping masks, opacity included) take the
// mouse; everything else clicks through. Per frame:
//   1. crop to the characters' bounding boxes, blit that area into a small
//      FBO at 1/kCell scale (linear filter = cheap box downsample);
//   2. glReadPixels into a PBO + fence — asynchronous, never stalls;
//   3. once a fence signals (1-3 frames later), map it, turn alpha into
//      rects (AlphaRegion::BuildRects) and set them as the input region.
// Before the first readback, or without GL 3.0 + ARB_sync, it falls back to
// one rectangle per character's bounding box.
namespace {

const int kCell = 4;          // 1 mask cell = kCell x kCell surface pixels
const uint8_t kAlpha = 8;     // cell alpha above this blocks the mouse
const int kSlots = 3;         // PBO ring depth

struct Slot
{
    GLuint pbo = 0;
    GLsync fence = nullptr;
    int left = 0, top = 0;    // crop origin, surface pixels (top-left origin)
    int width = 0, height = 0;// crop size, surface pixels
    int cw = 0, ch = 0;       // mask size, cells
};

struct State
{
    bool tried = false;
    bool ok = false;
    GLuint fbo = 0, rb = 0;
    int rbW = 0, rbH = 0;
    Slot slots[kSlots];
    int next = 0;             // slot to write this frame (= the oldest)
    bool haveMask = false;
    std::vector<AlphaRegion::Rect> cells, rects, committed;
    bool committedValid = false;
    std::vector<uint8_t> scratch;
};

State s;

bool Init()
{
    if (s.tried) return s.ok;
    s.tried = true;
    s.ok = GLEW_VERSION_3_0 && GLEW_ARB_sync;
    if (!s.ok) return false;
    glGenFramebuffers(1, &s.fbo);
    glGenRenderbuffers(1, &s.rb);
    for (Slot& slot : s.slots) glGenBuffers(1, &slot.pbo);
    return true;
}

// Map a signalled slot's pixels to surface-space rects in s.rects.
void Consume(Slot& slot)
{
    const size_t size = (size_t)slot.cw * slot.ch * 4;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
    const void* data = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, size, GL_MAP_READ_BIT);
    if (data)
    {
        // GL rows are bottom-up; flip into the top-down order Wayland uses.
        s.scratch.resize(size);
        const size_t stride = (size_t)slot.cw * 4;
        for (int y = 0; y < slot.ch; y++)
        {
            memcpy(&s.scratch[(size_t)y * stride],
                   (const uint8_t*)data + (size_t)(slot.ch - 1 - y) * stride, stride);
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

        AlphaRegion::BuildRects(s.scratch.data(), slot.cw, slot.ch, kAlpha, s.cells);

        // Cells -> surface pixels. The crop isn't always a multiple of
        // kCell, so scale by the real ratio and round outward.
        s.rects.clear();
        for (const AlphaRegion::Rect& c : s.cells)
        {
            const int x0 = slot.left + c.x * slot.width / slot.cw;
            const int y0 = slot.top + c.y * slot.height / slot.ch;
            const int x1 = slot.left + ((c.x + c.width) * slot.width + slot.cw - 1) / slot.cw;
            const int y1 = slot.top + ((c.y + c.height) * slot.height + slot.ch - 1) / slot.ch;
            s.rects.push_back(AlphaRegion::Rect{ x0, y0, x1 - x0, y1 - y0 });
        }
        s.haveMask = true;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glDeleteSync(slot.fence);
    slot.fence = nullptr;
}

// Harvest finished readbacks (oldest first), then queue this frame's.
void Capture(int surfaceH, int scale, int left, int top, int width, int height)
{
    for (int i = 0; i < kSlots; i++)
    {
        Slot& slot = s.slots[(s.next + i) % kSlots];
        if (!slot.fence) continue;
        if (glClientWaitSync(slot.fence, 0, 0) == GL_TIMEOUT_EXPIRED) break;
        Consume(slot);
    }

    Slot& slot = s.slots[s.next];
    if (slot.fence)
    {
        // Still in flight after a full ring: drop it rather than wait.
        glDeleteSync(slot.fence);
        slot.fence = nullptr;
    }

    const int cw = (width + kCell - 1) / kCell;
    const int ch = (height + kCell - 1) / kCell;
    if (cw > s.rbW || ch > s.rbH)
    {
        s.rbW = cw > s.rbW ? cw : s.rbW;
        s.rbH = ch > s.rbH ? ch : s.rbH;
        glBindRenderbuffer(GL_RENDERBUFFER, s.rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, s.rbW, s.rbH);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s.rb);
    }

    // The blit honours the scissor test; the renderer may leave it on.
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);

    // Crop is in surface pixels; the framebuffer is surface * scale.
    const int glY = (surfaceH - top - height) * scale; // GL origin is bottom-left
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.fbo);
    glBlitFramebuffer(left * scale, glY, (left + width) * scale, glY + height * scale,
                      0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, s.fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)cw * ch * 4, nullptr, GL_STREAM_READ);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (scissor) glEnable(GL_SCISSOR_TEST);

    slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    slot.left = left;
    slot.top = top;
    slot.width = width;
    slot.height = height;
    slot.cw = cw;
    slot.ch = ch;
    s.next = (s.next + 1) % kSlots;
}

} // namespace

void ResetWaylandInputRegion() {
    s.committedValid = false;
    s.haveMask = false;
}

void UpdateWaylandInputRegion(WaylandContext* wl, bool hidden) {
    if (!wl || !wl->compositor || !wl->surface) return;

    int width = wl->width;
    int height = wl->height;
    if (width <= 0 || height <= 0) return;

    // Character bounding boxes: the crop for the readback, and the fallback
    // region until a mask exists. Top-left-origin like Wayland.
    Csm::csmVector<LAppLive2DManager::ScreenRect> boxes;
    if (!hidden)
    {
        LAppLive2DManager::GetInstance()->GetCharacterPixelRects(width, height, boxes);
        // Subtitles take the mouse where they're drawn, like characters.
        const std::vector<Subtitles::Box>& subs = Subtitles::Get().Boxes();
        for (size_t i = 0; i < subs.size(); i++)
        {
            LAppLive2DManager::ScreenRect r;
            r.id = subs[i].character;
            r.left = subs[i].left < 0 ? 0 : subs[i].left;
            r.top = subs[i].top < 0 ? 0 : subs[i].top;
            r.width = subs[i].left + subs[i].width > width ? width - r.left : subs[i].left + subs[i].width - r.left;
            r.height = subs[i].top + subs[i].height > height ? height - r.top : subs[i].top + subs[i].height - r.top;
            if (r.width > 0 && r.height > 0) boxes.PushBack(r);
        }
    }

    std::vector<AlphaRegion::Rect> fallback;
    const std::vector<AlphaRegion::Rect>* want = &fallback; // empty = all click-through

    if (boxes.GetSize() == 0)
    {
        s.haveMask = false; // a stale mask must not reappear on show
    }
    else if (Init())
    {
        int l = boxes[0].left, t = boxes[0].top;
        int r = l + boxes[0].width, b = t + boxes[0].height;
        for (Csm::csmUint32 i = 1; i < boxes.GetSize(); i++)
        {
            if (boxes[i].left < l) l = boxes[i].left;
            if (boxes[i].top < t) t = boxes[i].top;
            if (boxes[i].left + boxes[i].width > r) r = boxes[i].left + boxes[i].width;
            if (boxes[i].top + boxes[i].height > b) b = boxes[i].top + boxes[i].height;
        }
        Capture(height, wl->scale, l, t, r - l, b - t);
    }

    if (s.haveMask)
    {
        want = &s.rects;
    }
    else
    {
        for (Csm::csmUint32 i = 0; i < boxes.GetSize(); i++)
        {
            fallback.push_back(AlphaRegion::Rect{ boxes[i].left, boxes[i].top,
                                                  boxes[i].width, boxes[i].height });
        }
    }

    // Identical region: skip the protocol traffic entirely.
    if (s.committedValid && *want == s.committed) return;

    wl_region* region = wl_compositor_create_region(wl->compositor);
    for (const AlphaRegion::Rect& r : *want)
    {
        wl_region_add(region, r.x, r.y, r.width, r.height);
    }
    wl_surface_set_input_region(wl->surface, region);
    wl_region_destroy(region);
    s.committed = *want;
    s.committedValid = true;
}
