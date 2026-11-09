// touch_input.cpp - the console's touchscreen as the pointer the ImGui menus understand. See
// touch_input.h. SDL's Switch video driver reports no pointer at all, so the samples are turned into
// the mouse events aurora and the ImGui SDL3 backend already handle.
#include "touch_input.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>
#include <switch.h>

#include <mutex>

namespace SwitchTouch {
namespace {

std::mutex g_mutex;
SDL_WindowID g_window = 0;
bool g_hidInitialized = false;
bool g_down = false;
float g_x = 0.0f;  // last position, in window coordinates
float g_y = 0.0f;

// The SDL Switch driver tracks the operation mode and resizes the window to the panel, but a mode
// switch can land between two samples, so scale from the panel the touch came from.
void PanelSize(int& width, int& height) {
    if (appletGetOperationMode() == AppletOperationMode_Console) {
        width = 1920;
        height = 1080;
    } else {
        width = 1280;
        height = 720;
    }
}

void PushMotion(SDL_WindowID window, float x, float y, bool down) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.common.timestamp = SDL_GetTicksNS();
    event.motion.windowID = window;
    event.motion.which = SDL_TOUCH_MOUSEID;
    event.motion.state = down ? SDL_BUTTON_LMASK : 0;
    event.motion.x = x;
    event.motion.y = y;
    SDL_PushEvent(&event);
}

void PushButton(SDL_WindowID window, float x, float y, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.common.timestamp = SDL_GetTicksNS();
    event.button.windowID = window;
    event.button.which = SDL_TOUCH_MOUSEID;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = down ? 1 : 0;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
}

} // namespace

void AttachWindow(void* sdlWindow) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_window = sdlWindow != nullptr ? SDL_GetWindowID(static_cast<SDL_Window*>(sdlWindow)) : 0;
}

void Pump() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_window == 0) {
        return;
    }
    if (!g_hidInitialized) {
        hidInitializeTouchScreen();
        g_hidInitialized = true;
    }
    HidTouchScreenState state{};
    if (hidGetTouchScreenStates(&state, 1) == 0) {
        return;  // no new sample
    }
    SDL_Window* window = SDL_GetWindowFromID(g_window);
    if (window == nullptr) {
        return;
    }
    if (state.count <= 0) {
        if (g_down) {
            PushButton(g_window, g_x, g_y, false);
            g_down = false;
        }
        return;
    }
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    int panelWidth = 0;
    int panelHeight = 0;
    PanelSize(panelWidth, panelHeight);
    const float x = panelWidth > 0 ? static_cast<float>(state.touches[0].x) * windowWidth / panelWidth : 0.0f;
    const float y = panelHeight > 0 ? static_cast<float>(state.touches[0].y) * windowHeight / panelHeight : 0.0f;
    if (x != g_x || y != g_y || !g_down) {
        g_x = x;
        g_y = y;
        PushMotion(g_window, x, y, g_down);
    }
    if (!g_down) {
        // Both in the same frame: ImGui needs the position before the press to hit the right widget.
        PushButton(g_window, x, y, true);
        g_down = true;
    }
}

} // namespace SwitchTouch

// Declared in aurora/switch_hooks.h; aurora calls it once the window exists.
extern "C" void MkwAttachTouchWindow(void* sdlWindow) {
    SwitchTouch::AttachWindow(sdlWindow);
}
