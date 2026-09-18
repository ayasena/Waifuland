#include "LAppWayland.hpp"
#include "LAppLive2DManager.hpp"
#include <Type/csmVector.hpp>

void UpdateWaylandInputRegion(WaylandContext* wl) {
    if (!wl || !wl->compositor || !wl->surface) return;

    int width = wl->width;
    int height = wl->height;
    if (width <= 0 || height <= 0) return;

    // One rectangle per character, derived from the same _modelMatrix state
    // hit-testing uses (LAppLive2DManager::GetCharacterPixelRects). Device
    // space is top-left-origin, matching Wayland surface coordinates, so no
    // Y flip is needed. No framebuffer readback, no per-frame allocation
    // beyond a handful of rects: O(characters) instead of O(pixels).
    Csm::csmVector<LAppLive2DManager::ScreenRect> rects;
    LAppLive2DManager::GetInstance()->GetCharacterPixelRects(width, height, rects);

    wl_region* region = wl_compositor_create_region(wl->compositor);
    for (Csm::csmUint32 i = 0; i < rects.GetSize(); i++)
    {
        wl_region_add(region, rects[i].left, rects[i].top,
                      rects[i].width, rects[i].height);
    }

    wl_surface_set_input_region(wl->surface, region);
    wl_region_destroy(region);
}
