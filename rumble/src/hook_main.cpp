// This file is the map of the hook translation unit. Each included fragment
// owns one responsibility, while the compiler still sees one unit. Keeping one
// unit is important here: the hooks share process state and exact function addresses.
// Dreamcast vibration requests are translated to XInput by a timed worker thread.

#include "rumble_runtime.inc"
#include "rumble_worker.inc"
#include "rumble_hooks.inc"
