// This file is the map of the hook translation unit. Each included fragment
// owns one responsibility, while the compiler still sees one unit. Keeping one
// unit is important here: the hooks share process state and exact function addresses.
// The data flow is catalog -> scene identity -> sequence dispatch -> Dreamcast renderer.

#include "audio_hook_catalog.inc"
#include "audio_hook_identity.inc"
#include "audio_hook_dispatch.inc"
#include "audio_hook_lifecycle.inc"
