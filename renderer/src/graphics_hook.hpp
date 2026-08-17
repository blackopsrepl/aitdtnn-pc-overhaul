#pragma once

#include "renderer_api.hpp"

namespace aitd4 {

bool install_graphics_hooks();
bool get_renderer_diagnostics(RendererDiagnostics* diagnostics);
bool request_renderer_capture(DWORD flags);

}  // namespace aitd4
