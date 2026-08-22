// This file is the map of the hook translation unit. Each included fragment
// owns one responsibility, while the compiler still sees one unit. Keeping one
// unit is important here: the hooks share process state and exact function addresses.
// This executable drives the renderer through a small real OpenGL window.

#include "gl_harness_support.inc"
#include "gl_harness_setup.inc"
#include "gl_harness_scenarios.inc"
