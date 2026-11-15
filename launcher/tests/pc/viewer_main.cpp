#include "app.hpp"
#include "demo_fixtures.hpp"
#include "menu_main.hpp"
#include "paths.hpp"
#include "viewer.hpp"

#include "switch.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

namespace fs = std::filesystem;
using namespace launcher;

namespace {

void press(App& app, Button button) {
    Controller controller;
    controller.m_kdown = static_cast<u64>(button);
    TouchInfo touch;
    app.TestUpdateTop(&controller, &touch);
}

} // namespace

int main(int argc, char** argv) {
    int run_seconds = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            run_seconds = std::atoi(argv[++i]);
        }
    }

    const fs::path root = "/tmp/opencode/launcher-viewer";
    SetRootDirectory((root / "launcher.nro").string().c_str());
    test::CreateDemoData(root, LAUNCHER_SOURCE_DIR "/launcher/assets/banners/newersmbw.png");

    if (!viewer::Init(1280, 720)) {
        return 1;
    }
    glfwSetWindowTitle(glfwGetCurrentContext(),
                       "NSMBW Launcher (PC)  |  Enter=A  Backspace=B  X=X  C=Y  - = Settings  Esc=Close");

    {
        App app;
        if (App::GetVg() == nullptr) {
            viewer::Shutdown();
            return 1;
        }
        app.Push(std::make_unique<ui::MainMenu>());
        app.TestSettlePop();

        std::cout << "Viewer controls:\n"
                     "  Enter / Z / Space : A\n"
                     "  Backspace         : B\n"
                     "  X                 : X (Game options)\n"
                     "  C                 : Y\n"
                     "  Minus             : SELECT (Settings)\n"
                     "  Equals            : START (Quit)\n"
                     "  Arrow keys        : D-pad\n"
                     "  Mouse             : click rows, entries and dialog buttons\n"
                     "  Escape            : close the window\n";

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(run_seconds);
        while (!app.IsQuitting() && !viewer::ShouldClose()) {
            if (run_seconds > 0 && std::chrono::steady_clock::now() >= deadline) {
                viewer::RequestClose();
                break;
            }
            viewer::Poll();

            while (true) {
                const int key = viewer::NextKey();
                if (key < 0) {
                    break;
                }
                Button button = Button::NONE;
                switch (key) {
                    case GLFW_KEY_ENTER:
                    case GLFW_KEY_KP_ENTER:
                    case GLFW_KEY_Z:
                    case GLFW_KEY_SPACE: button = Button::A; break;
                    case GLFW_KEY_BACKSPACE: button = Button::B; break;
                    case GLFW_KEY_X: button = Button::X; break;
                    case GLFW_KEY_C: button = Button::Y; break;
                    case GLFW_KEY_MINUS: button = Button::SELECT; break;
                    case GLFW_KEY_EQUAL:
                    case GLFW_KEY_KP_ADD: button = Button::START; break;
                    case GLFW_KEY_UP: button = Button::UP; break;
                    case GLFW_KEY_DOWN: button = Button::DOWN; break;
                    case GLFW_KEY_LEFT: button = Button::LEFT; break;
                    case GLFW_KEY_RIGHT: button = Button::RIGHT; break;
                    case GLFW_KEY_ESCAPE: viewer::RequestClose(); break;
                    default: break;
                }
                if (button != Button::NONE) {
                    press(app, button);
                }
            }

            float click_x = 0.f;
            float click_y = 0.f;
            while (viewer::NextClick(click_x, click_y)) {
                Controller controller;
                TouchInfo touch;
                touch.cur = TouchPoint{static_cast<s32>(click_x), static_cast<s32>(click_y)};
                touch.is_clicked = true;
                app.TestUpdateTop(&controller, &touch);
            }

            viewer::BeginFrame(app);
            app.RenderFrame();
            viewer::Present();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        std::cout << "viewer: loop exit quit=" << app.IsQuitting()
                  << " close=" << viewer::ShouldClose() << "\n";
    }

    viewer::Shutdown();
    return 0;
}
