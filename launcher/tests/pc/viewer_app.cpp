#include "app.hpp"
#include "pc_app_common.hpp"
#include "viewer.hpp"

#include "ui/widget.hpp"

#include <epoxy/gl.h>
#include <GLFW/glfw3.h>
#include <nanovg.h>
#define NANOVG_GL3 1
#include "nanovg_gl.h"

#include <cstdio>
#include <utility>
#include <vector>

namespace launcher::viewer {
namespace {
GLFWwindow* g_window{};
std::vector<int> g_keys;
std::vector<std::pair<float, float>> g_clicks;
bool g_close{};

void KeyCallback(GLFWwindow*, int key, int, int action, int) {
    if (action == GLFW_PRESS) {
        g_keys.push_back(key);
    }
}

void MouseCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) {
        return;
    }
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    g_clicks.emplace_back(static_cast<float>(x), static_cast<float>(y));
}
} // namespace

auto Init(int width, int height) -> bool {
    if (!glfwInit()) {
        std::fprintf(stderr, "viewer: glfwInit failed\n");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    g_window = glfwCreateWindow(width, height, "NSMBW Launcher (PC)", nullptr, nullptr);
    if (g_window == nullptr) {
        std::fprintf(stderr, "viewer: could not create the window\n");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);
    glfwSetKeyCallback(g_window, KeyCallback);
    glfwSetMouseButtonCallback(g_window, MouseCallback);
    glfwSetWindowCloseCallback(g_window, [](GLFWwindow*) {
        std::fprintf(stderr, "viewer: window close requested\n");
    });
    glfwSetWindowPos(g_window, 90, 60);
    glfwFocusWindow(g_window);
    return true;
}

void Poll() {
    glfwPollEvents();
}

void Present() {
    if (g_window != nullptr) {
        glfwSwapBuffers(g_window);
    }
}

void Shutdown() {
    if (g_window != nullptr) {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    glfwTerminate();
    g_keys.clear();
    g_clicks.clear();
}

auto ShouldClose() -> bool {
    return g_close || (g_window != nullptr && glfwWindowShouldClose(g_window));
}

void RequestClose() {
    g_close = true;
}

auto NextKey() -> int {
    if (g_keys.empty()) {
        return -1;
    }
    const int key = g_keys.front();
    g_keys.erase(g_keys.begin());
    return key;
}

auto NextClick(float& x, float& y) -> bool {
    if (g_clicks.empty()) {
        return false;
    }
    x = g_clicks.front().first;
    y = g_clicks.front().second;
    g_clicks.erase(g_clicks.begin());
    return true;
}

void BeginFrame(App& app) {
    int fb_width = 0;
    int fb_height = 0;
    glfwGetFramebufferSize(g_window, &fb_width, &fb_height);
    if (fb_width <= 0 || fb_height <= 0) {
        return;
    }
    glViewport(0, 0, fb_width, fb_height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    app.SetPixelRatio(static_cast<float>(fb_width) / SCREEN_WIDTH);
}

} // namespace launcher::viewer

namespace launcher {

App::App() {
    pc::CurrentApp() = this;
    pc::LoadLogo();

    m_vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (m_vg == nullptr) {
        std::fprintf(stderr, "viewer: nvgCreateGL3 failed\n");
        return;
    }
    pc::LoadFonts(m_vg);
    pc::LoadTheme(Theme());
}

App::~App() {
    m_widgets.clear();
    if (m_vg != nullptr) {
        nvgDeleteGL3(m_vg);
        m_vg = nullptr;
    }
    if (pc::CurrentApp() == this) {
        pc::CurrentApp() = nullptr;
    }
}

auto App::RenderScreenshot(const std::string&) -> bool {
    return false;
}

} // namespace launcher
