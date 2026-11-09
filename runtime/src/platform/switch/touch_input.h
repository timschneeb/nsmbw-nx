#pragma once

namespace SwitchTouch {

// The window the synthesized pointer events address (an SDL_Window*). Called from aurora once it
// has created the window.
void AttachWindow(void* sdlWindow);

// Turns a touchscreen sample into the mouse events the ImGui menus read. Once per frame.
void Pump();

} // namespace SwitchTouch
