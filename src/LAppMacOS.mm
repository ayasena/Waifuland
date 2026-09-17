// Copyright(c) Live2D Inc. All rights reserved.
//
// Use of this source code is governed by the Live2D Open Software license
// that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
//
// macOS Quartz/Cocoa windowing backend. Mirrors LAppWayland.cpp's contract:
// a full-screen transparent floating window, click-through outside the
// model's opaque pixels, and multi-monitor drag-switch.

#include <GL/glew.h>
#import <Cocoa/Cocoa.h>
#include <ApplicationServices/ApplicationServices.h>
#include <vector>

#include "LAppMacOS.hpp"
#include "LAppDelegate.hpp"
#include "LAppPal.hpp"

// Overlay window/view never take key/main status or first-responder focus —
// the mascot has no keyboard interaction and must never steal focus from
// whatever the user is actually working in, matching
// zwlr_layer_surface_v1_set_keyboard_interactivity(..., 0) on the Wayland side.
@interface WaifulandOverlayView : NSOpenGLView
@end
@implementation WaifulandOverlayView
- (BOOL)acceptsFirstResponder { return NO; }
- (BOOL)isOpaque { return NO; }
@end

@interface WaifulandWindow : NSWindow
@end
@implementation WaifulandWindow
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

static MacOSContext* g_mac = nullptr;

static void RefreshOutputs(MacOSContext* mac) {
    for (size_t i = 0; i < mac->outputs.size(); i++) {
        delete mac->outputs[i];
    }
    mac->outputs.clear();

    CGDirectDisplayID displays[32];
    uint32_t count = 0;
    CGGetActiveDisplayList(32, displays, &count);

    for (uint32_t i = 0; i < count; i++) {
        CGRect bounds = CGDisplayBounds(displays[i]);
        MacOSContext::OutputInfo* info = new MacOSContext::OutputInfo();
        info->displayID = displays[i];
        info->x = (int)bounds.origin.x;
        info->y = (int)bounds.origin.y;
        info->width = (int)bounds.size.width;
        info->height = (int)bounds.size.height;
        snprintf(info->name, sizeof(info->name), "display-%u", displays[i]);
        mac->outputs.push_back(info);
    }
}

// CGEventGetLocation returns points in the same top-left-origin global space
// as CGDisplayBounds, so this lines up directly with OutputInfo's x/y/width/height
// (built from CGDisplayBounds too) with no extra conversion — unlike Wayland,
// which needs compositor-specific IPC (Hyprland's socket) to get this at all.
bool GetGlobalCursorPosition(int& x, int& y) {
    CGEventRef event = CGEventCreate(NULL);
    CGPoint pt = CGEventGetLocation(event);
    CFRelease(event);
    x = (int)pt.x;
    y = (int)pt.y;
    return true;
}

bool SetupMacOSContext(MacOSContext* mac, int /*width*/, int /*height*/) {
    g_mac = mac;

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    RefreshOutputs(mac);
    if (mac->outputs.empty()) {
        LAppPal::PrintLogLn("[macOS] No active displays found");
        return false;
    }
    mac->current_output_index = 0;

    NSScreen* mainScreen = [NSScreen mainScreen];
    NSRect frame = [mainScreen frame];

    WaifulandWindow* window = [[WaifulandWindow alloc] initWithContentRect:frame
                                                                   styleMask:NSWindowStyleMaskBorderless
                                                                     backing:NSBackingStoreBuffered
                                                                       defer:NO];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setHasShadow:NO];
    [window setLevel:NSStatusWindowLevel];
    [window setCollectionBehavior:(NSWindowCollectionBehaviorCanJoinAllSpaces |
                                    NSWindowCollectionBehaviorStationary |
                                    NSWindowCollectionBehaviorIgnoresCycle |
                                    NSWindowCollectionBehaviorFullScreenAuxiliary)];
    // Start click-through everywhere; UpdateMacOSInputRegion toggles this
    // per-frame based on whether the cursor sits over an opaque model pixel.
    [window setIgnoresMouseEvents:YES];

    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAColorSize, 32,
        NSOpenGLPFAAlphaSize, 8,
        NSOpenGLPFADepthSize, 24,
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersionLegacy,
        0
    };
    NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    if (!pixelFormat) {
        LAppPal::PrintLogLn("[macOS] Failed to create NSOpenGLPixelFormat");
        return false;
    }

    WaifulandOverlayView* view = [[WaifulandOverlayView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)
                                                                  pixelFormat:pixelFormat];
    [pixelFormat release]; // the view retains it internally
    [view setWantsBestResolutionOpenGLSurface:NO]; // Track logical points, matching RenderTargetWidth/Height semantics

    // -openGLContext is a "get" accessor (not owned); retain explicitly since
    // we hold onto it for the app's lifetime in a manual-refcount (non-ARC) file.
    NSOpenGLContext* glContext = [[view openGLContext] retain];
    GLint swapInterval = 1;
    [glContext setValues:&swapInterval forParameter:NSOpenGLContextParameterSwapInterval];
    GLint surfaceOpacity = 0;
    [glContext setValues:&surfaceOpacity forParameter:NSOpenGLContextParameterSurfaceOpacity];

    [window setContentView:view];
    [glContext makeCurrentContext];

    [window orderFrontRegardless];
    [NSApp activateIgnoringOtherApps:YES];

    // Local monitor: only fires while the window actually accepts the event
    // (i.e. cursor over an opaque model pixel), which is exactly the
    // "input region" semantics wl_pointer button events get from the
    // compositor on the Wayland side.
    [NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp |
                                                    NSEventMaskRightMouseDown | NSEventMaskRightMouseUp |
                                                    NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp |
                                                    NSEventMaskScrollWheel)
                                          handler:^NSEvent*(NSEvent* event) {
        if ([event window] != window) {
            return event;
        }
        switch (event.type) {
            case NSEventTypeLeftMouseDown:
                LAppDelegate::GetInstance()->OnMouseCallBack(nullptr, 0, 1, 0);
                break;
            case NSEventTypeLeftMouseUp:
                LAppDelegate::GetInstance()->OnMouseCallBack(nullptr, 0, 0, 0);
                break;
            case NSEventTypeRightMouseDown:
                LAppDelegate::GetInstance()->OnMouseCallBack(nullptr, 1, 1, 0);
                break;
            case NSEventTypeRightMouseUp:
                LAppDelegate::GetInstance()->OnMouseCallBack(nullptr, 1, 0, 0);
                break;
            case NSEventTypeOtherMouseDown:
                LAppDelegate::GetInstance()->OnMouseCallBack(nullptr, 2, 1, 0);
                break;
            case NSEventTypeOtherMouseUp:
                LAppDelegate::GetInstance()->OnMouseCallBack(nullptr, 2, 0, 0);
                break;
            case NSEventTypeScrollWheel:
                LAppDelegate::GetInstance()->OnScrollCallBack(nullptr, 0, event.scrollingDeltaY > 0 ? 1 : -1);
                break;
            default:
                break;
        }
        return event;
    }];

    mac->window = (void*)window;
    mac->view = (void*)view;
    mac->glContext = (void*)glContext;
    mac->width = (int)frame.size.width;
    mac->height = (int)frame.size.height;

    LAppPal::PrintLogLn("[macOS] Overlay window ready: %dx%d on %zu display(s)", mac->width, mac->height, mac->outputs.size());
    return true;
}

void CleanMacOSContext(MacOSContext* mac) {
    if (!mac) return;

    if (mac->glContext) {
        [NSOpenGLContext clearCurrentContext];
        [(NSOpenGLContext*)mac->glContext release];
        mac->glContext = nullptr;
    }
    if (mac->view) {
        [(NSView*)mac->view release];
        mac->view = nullptr;
    }
    if (mac->window) {
        NSWindow* window = (NSWindow*)mac->window;
        [window orderOut:nil];
        [window release];
        mac->window = nullptr;
    }
    for (size_t i = 0; i < mac->outputs.size(); i++) {
        delete mac->outputs[i];
    }
    mac->outputs.clear();
    g_mac = nullptr;
}

void MacOSPumpEvents() {
    @autoreleasepool {
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES]) != nil) {
            [NSApp sendEvent:event];
        }
    }
}

void MacOSSwapBuffers(MacOSContext* mac) {
    if (!mac || !mac->glContext) return;
    NSOpenGLContext* ctx = (NSOpenGLContext*)mac->glContext;
    [ctx flushBuffer];
}

void UpdateMacOSInputRegion(MacOSContext* mac, int hx, int hy) {
    if (!mac || !mac->window) return;

    int width = mac->width;
    int height = mac->height;
    if (width <= 0 || height <= 0) return;

    NSWindow* window = (NSWindow*)mac->window;

    // CGEventGetLocation / CGDisplayBounds are top-left-origin; NSWindow
    // frames are bottom-left-origin relative to the primary display, and
    // OpenGL's row 0 is the bottom row — flip once against the primary
    // display height to land in the same bottom-up space glReadPixels uses.
    CGRect mainBounds = CGDisplayBounds(CGMainDisplayID());
    int cocoaX = hx;
    int cocoaY = (int)mainBounds.size.height - hy;

    NSRect frame = [window frame];
    int localX = cocoaX - (int)frame.origin.x;
    int localY = cocoaY - (int)frame.origin.y;

    bool opaque = false;
    if (localX >= 0 && localX < width && localY >= 0 && localY < height) {
        // Only the pixel under the cursor is ever tested — read just that
        // one, not the whole framebuffer (was a full-window glReadPixels
        // every frame, e.g. ~20MB + a GPU pipeline stall on a Retina
        // display, 60x/sec). GL_BGRA matches macOS's native framebuffer
        // layout and avoids the driver-side per-pixel RGBA conversion.
        unsigned char pixel[4];
        glReadPixels(localX, localY, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE, pixel);
        opaque = pixel[3] > 10;
    }

    [window setIgnoresMouseEvents:!opaque];
}

void SwitchMacOSOutputToMonitor(int hx, int hy) {
    if (!g_mac || g_mac->outputs.empty()) return;

    int target_idx = g_mac->current_output_index;
    for (size_t i = 0; i < g_mac->outputs.size(); i++) {
        MacOSContext::OutputInfo* out = g_mac->outputs[i];
        if (hx >= out->x && hx < out->x + out->width && hy >= out->y && hy < out->y + out->height) {
            target_idx = (int)i;
            break;
        }
    }
    if (target_idx == g_mac->current_output_index) return;

    LAppPal::PrintLogLn("[macOS] Switching output from %d to %d", g_mac->current_output_index, target_idx);
    g_mac->current_output_index = target_idx;
    uint32_t displayID = g_mac->outputs[target_idx]->displayID;

    NSWindow* window = (NSWindow*)g_mac->window;
    NSView* view = (NSView*)g_mac->view;

    for (NSScreen* screen in [NSScreen screens]) {
        NSNumber* num = screen.deviceDescription[@"NSScreenNumber"];
        if (num && [num unsignedIntValue] == displayID) {
            NSRect frame = [screen frame];
            [window setFrame:frame display:YES];
            [view setFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
            g_mac->width = (int)frame.size.width;
            g_mac->height = (int)frame.size.height;
            break;
        }
    }
}

void MoveToFocusedMonitor() {
    // macOS has no per-compositor "focused monitor" IPC to query (unlike
    // Hyprland's hyprctl); the monitor under the current cursor is the
    // closest equivalent and matches what SIGUSR2 is used for in practice.
    int hx, hy;
    if (!GetGlobalCursorPosition(hx, hy)) return;
    SwitchMacOSOutputToMonitor(hx, hy);
}
