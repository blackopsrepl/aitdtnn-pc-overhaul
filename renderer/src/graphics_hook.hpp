#pragma once

// Narrow boundary of the graphics hook unit. Callers can install it and inspect
// diagnostics, but cannot mutate its tightly shared OpenGL and Bink state.

#include "renderer_api.hpp"

namespace aitd4 {

bool install_graphics_hooks();
bool get_renderer_diagnostics(RendererDiagnostics* diagnostics);
bool request_renderer_capture(DWORD flags);

}  // namespace aitd4
