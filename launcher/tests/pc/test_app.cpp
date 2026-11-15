#include "app.hpp"
#include "pc_app_common.hpp"

#include "ui/widget.hpp"

namespace launcher {

App::App() {
    pc::CurrentApp() = this;
}

App::~App() {
    m_widgets.clear();
    if (pc::CurrentApp() == this) {
        pc::CurrentApp() = nullptr;
    }
}

auto App::RenderScreenshot(const std::string&) -> bool {
    return false;
}

} // namespace launcher
