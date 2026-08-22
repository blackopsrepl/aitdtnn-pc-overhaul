// This file is the map of the hook translation unit. Each included fragment
// owns one responsibility, while the compiler still sees one unit. Keeping one
// unit is important here: the hooks share process state and exact function addresses.
// The proxy validates the game, starts every module, then releases the original entrypoint.

#include "version_proxy_runtime.inc"
#include "version_proxy_modules.inc"
#include "version_proxy_exports.inc"
