#pragma once

#ifdef __APPLE__
#include "LAppMacOS.hpp"
typedef MacOSContext PlatformContext;
#else
#include "LAppWayland.hpp"
typedef WaylandContext PlatformContext;
#endif
