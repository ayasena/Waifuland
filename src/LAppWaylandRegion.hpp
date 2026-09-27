#pragma once
struct WaylandContext;
// Call after rendering, before eglSwapBuffers (the swap commits the region).
void UpdateWaylandInputRegion(WaylandContext* wl, bool hidden);
