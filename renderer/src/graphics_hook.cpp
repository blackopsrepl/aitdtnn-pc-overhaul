// This file is the map of the hook translation unit. Each included fragment
// owns one responsibility, while the compiler still sees one unit. Keeping one
// unit is important here: the hooks share process state and exact function addresses.
// Start here, then follow the include list from platform declarations to presentation.

#include "graphics_hook_platform.inc"
#include "graphics_hook_state.inc"
#include "graphics_hook_window.inc"
#include "graphics_hook_movie_requests.inc"
#include "graphics_hook_movie_decode.inc"
#include "graphics_hook_movie_lifecycle.inc"
#include "graphics_hook_context.inc"
#include "graphics_hook_crt.inc"
#include "graphics_hook_framebuffer.inc"
#include "graphics_hook_legacy_gl.inc"
#include "graphics_hook_present.inc"
#include "graphics_hook_routing.inc"
