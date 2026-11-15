#pragma once

namespace launcher {
class App;
}

namespace launcher::viewer {

auto Init(int width, int height) -> bool;
void BeginFrame(App& app);
void Poll();
void Present();
void Shutdown();
auto ShouldClose() -> bool;
void RequestClose();
// Returns the next pressed GLFW key, or -1 when the queue is empty.
auto NextKey() -> int;
// Returns the next left click position in UI coordinates.
auto NextClick(float& x, float& y) -> bool;

} // namespace launcher::viewer
