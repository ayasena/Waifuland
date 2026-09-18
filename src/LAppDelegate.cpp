#include <signal.h>
/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <libgen.h>
#include <climits>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <GL/glew.h>

#include "LAppView.hpp"
#include "LAppPal.hpp"
#ifdef __APPLE__
// LAppMacOS.hpp already pulled in via LAppPlatform.hpp -> LAppDelegate.hpp
#else
#include "LAppWaylandRegion.hpp"
#endif
#include "LAppDefine.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppTextureManager.hpp"
#include "LAppIPC.hpp"

using namespace Csm;
using namespace std;
using namespace LAppDefine;

namespace {
    LAppDelegate* s_instance = NULL;

    // Signal handlers only raise these flags; the main loop applies them.
    // (Calling ToggleHidden()/RequestMoveToFocusedMonitor() directly from a
    // handler would mutate live loop state from async context.)
    volatile sig_atomic_t s_toggleRequested = 0;
    volatile sig_atomic_t s_focusRequested = 0;
}

static void HandleToggleSignal(int) {
    s_toggleRequested = 1;
}

static void HandleFocusSignal(int) {
    s_focusRequested = 1;
}

LAppDelegate* LAppDelegate::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = new LAppDelegate();
    }

    return s_instance;
}

void LAppDelegate::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
    }

    s_instance = NULL;
}

bool LAppDelegate::Initialize()
{
    if (DebugLogEnable)
    {
        LAppPal::PrintLogLn("START");
    }

    signal(SIGUSR1, HandleToggleSignal);
    signal(SIGUSR2, HandleFocusSignal);

    _windowWidth = RenderTargetWidth;
    _windowHeight = RenderTargetHeight;

#ifdef __APPLE__
    if (!SetupMacOSContext(&_wlContext, RenderTargetWidth, RenderTargetHeight)) {
        return false;
    }
#else
    DetectCompositor();

    if (!SetupWaylandContext(&_wlContext, RenderTargetWidth, RenderTargetHeight)) {
        return false;
    }
#endif

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        if (DebugLogEnable)
        {
            LAppPal::PrintLogLn("Can't initilize glew. Error: %s", glewGetErrorString(err));
        }
        return false;
    }

    //テクスチャサンプリング設定
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    //透過設定
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

#ifdef __APPLE__
    glViewport(0, 0, _wlContext.backingWidth, _wlContext.backingHeight);
#else
    glViewport(0, 0, _windowWidth, _windowHeight);
#endif

    // Cubism3の初期化
    InitializeCubism();
    SetExecuteAbsolutePath();
    LAppLive2DManager::GetInstance();

    _view->Initialize(_windowWidth, _windowHeight);

    // Initialize IPC socket server
    LAppIPC::GetInstance()->Initialize();

    return true;
}
void LAppDelegate::Release()
{
    LAppIPC::ReleaseInstance();

    // Models must die before the texture manager: their destructors return
    // texture references to it. (Live2DManager::ReleaseInstance below then
    // finds an empty roster and only tears down shared state.)
    LAppLive2DManager::GetInstance()->ReleaseAllModel();

#ifdef __APPLE__
    CleanMacOSContext(&_wlContext);
#else
    CleanWaylandContext(&_wlContext);
#endif

    delete _textureManager;
    delete _view;

    LAppLive2DManager::ReleaseInstance();
    CubismFramework::Dispose();
}
void LAppDelegate::Run()
{
    while (!_isEnd)
    {
        // Apply deferred signal requests (see HandleToggleSignal/).
        if (s_toggleRequested) {
            s_toggleRequested = 0;
            ToggleHidden();
        }
        if (s_focusRequested) {
            s_focusRequested = 0;
            RequestMoveToFocusedMonitor();
        }

#ifdef __APPLE__
        MacOSPumpEvents();
#else
        if (wl_display_dispatch_pending(_wlContext.display) == -1) {
            break;
        }
        wl_display_flush(_wlContext.display);
#endif

        int width = _wlContext.width;
        int height = _wlContext.height;

        if((_windowWidth!=width || _windowHeight!=height) && width>0 && height>0) {
            _view->Initialize(width, height);
            LAppLive2DManager::GetInstance()->SetRenderTargetSize(width, height);
            _windowWidth = width;
            _windowHeight = height;
        }

#ifdef __APPLE__
        glViewport(0, 0, _wlContext.backingWidth, _wlContext.backingHeight);
#else
        glViewport(0, 0, _windowWidth, _windowHeight);
#endif

        if (_pendingFocusMove) {
            _pendingFocusMove = false;
            MoveToFocusedMonitor();
        }

        int hx, hy;
        if (GetGlobalCursorPosition(hx, hy) && !_wlContext.outputs.empty()) {
            int current_idx = _wlContext.current_output_index;
            PlatformContext::OutputInfo* out = _wlContext.outputs[current_idx];
            
            int local_x = hx - out->x;
            int local_y = hy - out->y;
            OnMouseCallBack(nullptr, (double)local_x, (double)local_y);
        }

        // Poll IPC commands
        LAppIPC::GetInstance()->Poll();

        LAppPal::UpdateTime();

        // Per-character zoom easing now happens inside
        // LAppLive2DManager::OnUpdate(), called from _view->Render() below.

        // 画面の初期化 -> Transparent!
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearDepth(1.0);

        if (!_isHidden) {
            _view->Render();
        }

#ifdef __APPLE__
        UpdateMacOSInputRegion(&_wlContext, hx, hy);
        MacOSSwapBuffers(&_wlContext);
#else
        UpdateWaylandInputRegion(&_wlContext);
        eglSwapBuffers(_wlContext.egl_display, _wlContext.egl_surface);
#endif

        if (_isHidden) {
            usleep(33000); // Reduce CPU usage when hidden
        }
    }
    Release();
    LAppDelegate::ReleaseInstance();
}
LAppDelegate::LAppDelegate():
    _cubismOption(),
    
    _captured(false),
    _mouseX(0.0f),
    _mouseY(0.0f),
    _isEnd(false),
    _windowWidth(0),
    _windowHeight(0),
    _isDraggingWindow(false),
    _dragStartX(0),
    _dragStartY(0),
    _windowStartX(0),
    _windowStartY(0),
    _lookCenterX(0.5f),
    _lookCenterY(0.5f),
    _draggedCharacterId(-1)
{
    _executeAbsolutePath = "";
    _view = new LAppView();
    _textureManager = new LAppTextureManager();
}

LAppDelegate::~LAppDelegate()
{

}

void LAppDelegate::InitializeCubism()
{
    //setup cubism
    _cubismOption.LogFunction = LAppPal::PrintMessage;
    _cubismOption.LoggingLevel = LAppDefine::CubismLoggingLevel;
    _cubismOption.LoadFileFunction = LAppPal::LoadFileAsBytes;
    _cubismOption.ReleaseBytesFunction = LAppPal::ReleaseBytes;
    Csm::CubismFramework::StartUp(&_cubismAllocator, &_cubismOption);

    //Initialize cubism
    CubismFramework::Initialize();

    //default proj
    CubismMatrix44 projection;

    
        int hx, hy;
        if (GetGlobalCursorPosition(hx, hy) && !_wlContext.outputs.empty()) {
            int current_idx = _wlContext.current_output_index;
            PlatformContext::OutputInfo* out = _wlContext.outputs[current_idx];
            
            int local_x = hx - out->x;
            int local_y = hy - out->y;
            OnMouseCallBack(nullptr, (double)local_x, (double)local_y);
        }

        LAppPal::UpdateTime();
}

void LAppDelegate::OnMouseCallBack(void* window, int button, int action, int modify)
{
    if (_view == NULL)
    {
        return;
    }
    
    if (button == 0)
    {
        if (1 == action)
        {
            _captured = true;
            _view->OnTouchesBegan(_mouseX, _mouseY);

            // Which character (if any) this drag gesture targets — routes
            // subsequent move deltas to only that character.
            float sx = _view->TransformScreenX(_mouseX);
            float sy = _view->TransformScreenY(_mouseY);
            _draggedCharacterId = LAppLive2DManager::GetInstance()->HitTestCharacter(sx, sy);

            // Start drag
            _isDraggingWindow = true;
            double curX, curY;
            curX = _mouseX; curY = _mouseY;
            _dragStartX = static_cast<int>(curX);
            _dragStartY = static_cast<int>(curY);
            _windowStartX = static_cast<int>(curX); _windowStartY = static_cast<int>(curY);

        }
        else if (0 == action)
        {
            if (_captured)
            {
                _captured = false;

                // Execute screen switch on release
                int hx, hy;
                bool is_drag = _isDraggingWindow;
                bool got_cursor = GetGlobalCursorPosition(hx, hy);
                bool has_outputs = !_wlContext.outputs.empty();
                LAppPal::PrintLogLn("[Debug] Release: is_drag=%d, got_cursor=%d, has_outputs=%d, hx=%d, hy=%d", 
                    (int)is_drag, (int)got_cursor, (int)has_outputs, hx, hy);
                
                if (is_drag && got_cursor && has_outputs) {
                    int old_idx = _wlContext.current_output_index;
#ifdef __APPLE__
                    extern void SwitchMacOSOutputToMonitor(int, int);
                    SwitchMacOSOutputToMonitor(hx, hy);
#else
                    extern void SwitchWaylandOutputToMonitor(int, int);
                    SwitchWaylandOutputToMonitor(hx, hy);
#endif
                    int new_idx = _wlContext.current_output_index;
                    // ponytail: dragging a character across a monitor boundary
                    // still moves the whole window (all characters move with
                    // it — inherent to sharing one OS window). It no longer
                    // rescales/repositions a single "the model" on arrival the
                    // way the old single-character build did, since with
                    // multiple independently-positioned characters there's no
                    // one position to rescale. Per-character re-layout for the
                    // new monitor's resolution, if wanted, would go here.
                    (void)old_idx; (void)new_idx;
                }

                _isDraggingWindow = false;

                double curX, curY;
                curX = _mouseX; curY = _mouseY;
                int dx = abs(static_cast<int>(curX) - _windowStartX);
                int dy = abs(static_cast<int>(curY) - _windowStartY);
                LAppPal::PrintLogLn("[Debug] Tap check: cur(%d,%d) start(%d,%d) dx=%d dy=%d",
                    static_cast<int>(curX), static_cast<int>(curY), _windowStartX, _windowStartY, dx, dy);
                if (dx < 10 && dy < 10)
                {
                    LAppPal::PrintLogLn("[Event] Model Tapped: Cursor (%d, %d)", static_cast<int>(curX), static_cast<int>(curY));
                      _view->OnTouchesEnded(_mouseX, _mouseY); // Trigger Tap
                }
                else
                {
                    LAppLive2DManager::GetInstance()->OnDrag(0.0f, 0.0f); // End look
                }

                _draggedCharacterId = -1;
            }
        }
    }
    else if (button == 1 && action == 0)
    {
        // Switch this character's model (only the one under the cursor)
        float sx = _view->TransformScreenX(_mouseX);
        float sy = _view->TransformScreenY(_mouseY);
        int id = LAppLive2DManager::GetInstance()->HitTestCharacter(sx, sy);
        if (id >= 0)
        {
            LAppPal::PrintLogLn("[Event] Switch Model Triggered (character %d)", id);
            LAppLive2DManager::GetInstance()->NextCharacterModel(id);
        }
    }
    else if (button == 2 && action == 1)
    {
        // Switch this character's skin (only the one under the cursor)
        float sx = _view->TransformScreenX(_mouseX);
        float sy = _view->TransformScreenY(_mouseY);
        int id = LAppLive2DManager::GetInstance()->HitTestCharacter(sx, sy);
        if (id >= 0)
        {
            LAppPal::PrintLogLn("[Event] Switch Skin Triggered (character %d)", id);
            LAppLive2DManager::GetInstance()->SwitchSkin(id);
        }
    }
}

void LAppDelegate::OnMouseCallBack(void* window, double x, double y)
{
    _mouseX = static_cast<float>(x);
    _mouseY = static_cast<float>(y);

    if (_view == NULL)
    {
        return;
    }

    // Calculate viewX / viewY based on look center. Shared across every
    // character (each has its own drag/look state — see
    // LAppLive2DManager::OnDrag), so this is deliberately not per-character.
    int width, height;
    width = _windowWidth; height = _windowHeight;

    float faceCenterX = (float)width * _lookCenterX;
    float faceCenterY = (float)height * _lookCenterY;

    float viewX = (_mouseX - faceCenterX) / ((float)width / 2.0f);
    float viewY = -(_mouseY - faceCenterY) / ((float)height / 2.0f);

    LAppLive2DManager::GetInstance()->OnDrag(viewX, viewY);

    if (_captured && _isDraggingWindow && _draggedCharacterId >= 0)
    {
        double curX = x;
        double curY = y;
        int deltaX = static_cast<int>(curX) - _dragStartX;
        int deltaY = static_cast<int>(curY) - _dragStartY;

        if (deltaX != 0 || deltaY != 0) {
            float dx_logical = (float)deltaX / (float)_windowHeight * 2.0f;
            float dy_logical = -(float)deltaY / (float)_windowHeight * 2.0f; // Y axis is flipped in OpenGL

            LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
            float newX = mgr->GetCharacterX(_draggedCharacterId) + dx_logical;
            float newY = mgr->GetCharacterY(_draggedCharacterId) + dy_logical;
            mgr->SetCharacterPosition(_draggedCharacterId, newX, newY);

            _dragStartX = static_cast<int>(curX);
            _dragStartY = static_cast<int>(curY);
        }
    }
}

void LAppDelegate::OnScrollCallBack(void* window, double xoffset, double yoffset)
{
    if (_view == NULL) return;

    float sx = _view->TransformScreenX(_mouseX);
    float sy = _view->TransformScreenY(_mouseY);
    int id = LAppLive2DManager::GetInstance()->HitTestCharacter(sx, sy);
    if (id < 0) return;

    // Trackpad fires one event per ~pixel of scroll, each clamped to yoffset
    // ±1 — 10%/event compounded over a whole gesture blew past any usable
    // range almost instantly. 2%/event still feels responsive.
    float scale = 1.0f + (yoffset * 0.02f);

    LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
    float target = mgr->GetCharacterZoom(id) * scale;
    if (target < 0.1f) target = 0.1f;
    if (target > 10.0f) target = 10.0f;
    mgr->SetCharacterZoom(id, target, sx, sy);
}

void LAppDelegate::GetClientSize(int& rWidth, int& rHeight)
{
    rWidth = GetInstance()->_windowWidth;
    rHeight = GetInstance()->_windowHeight;
}

void LAppDelegate::SetExecuteAbsolutePath()
{
    char path[1024];
#ifdef __APPLE__
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
    {
        path[0] = '\0';
    }
    char resolved[PATH_MAX];
    if (realpath(path, resolved) != NULL)
    {
        strncpy(path, resolved, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
#else
    ssize_t len = readlink("/proc/self/exe", path, 1024 - 1);
    if (len != -1)
    {
        path[len] = '\0';
    }
#endif
    this->_executeAbsolutePath = dirname(path);
    this->_executeAbsolutePath += "/";
}
