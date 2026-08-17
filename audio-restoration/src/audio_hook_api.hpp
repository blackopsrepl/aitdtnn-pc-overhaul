#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Shared explicit-initializer ABI. The argument is reserved for the common
// loader contract and must currently be null or ignored by each module.
extern "C" DWORD WINAPI AITD4_AudioInitialize(void* reserved);
