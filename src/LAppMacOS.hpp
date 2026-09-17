#pragma once

#include <vector>
#include <cstdint>

struct MacOSContext {
    void* window = nullptr;     ///< NSWindow*
    void* view = nullptr;       ///< NSOpenGLView*
    void* glContext = nullptr;  ///< NSOpenGLContext*

    struct OutputInfo {
        uint32_t displayID;
        int x;
        int y;
        int width;
        int height;
        char name[32];
    };
    std::vector<OutputInfo*> outputs;
    int current_output_index = 0;

    int width = 0;
    int height = 0;
};

bool GetGlobalCursorPosition(int& x, int& y);

bool SetupMacOSContext(MacOSContext* mac, int width, int height);
void CleanMacOSContext(MacOSContext* mac);

void MacOSPumpEvents();
void MacOSSwapBuffers(MacOSContext* mac);
void UpdateMacOSInputRegion(MacOSContext* mac, int cursorX, int cursorY);

void SwitchMacOSOutputToMonitor(int hx, int hy);
void MoveToFocusedMonitor();
