# AITD:TNN PC Dreamcast music restoration

This module makes the Windows game render interactive music and ambience through
the Dreamcast release's Manatee driver, program maps, sample banks and AICA
synthesis. Live sequence events are taken from the PC engine; extracted
Dreamcast DSEQ data is used for identity evidence, not independently scheduled.
The module therefore restores the Dreamcast synthesis/bank path but does not
claim to replace a genuinely different PC scene-to-cue decision. PC sound
effects and native FMV audio remain on their original paths.

The AICA core is output-only: it never opens WinMM or a second audio device.
The hook captures the game's actual Miles `HDIGDRIVER` through its imported
`AIL_waveOutOpen` and `AIL_allocate_sample_handle` calls, allocates a dedicated
Miles sample, and continuously submits two locked 44.1 kHz stereo PCM buffers.
Persistent PC music dispatch is suppressed from the first hooked event; there
is no PC-music fallback. Short MIDI sound effects and FMV audio are unchanged.

No copyrighted game or Dreamcast assets are included. The combined overhaul
wizard derives the required runtime catalog locally from a Dreamcast disc image
provided by the user.

The monorepo's app-local `version.dll` loader initializes the uniquely named
`audio-restoration\aitd4-audio-hook.dll` before the renderer and rumble modules.
It leaves `alone4.exe` byte-for-byte unchanged and requires this fail-closed
export:

```cpp
extern "C" DWORD WINAPI AITD4_AudioInitialize(void* reserved);
```

Runtime assets and diagnostics live under `audio-restoration`. Container
provenance and live sequence evidence disambiguate containers with identical
program maps; scene replacement explicitly tears down active notes and refuses
to render an unresolved persistent music bank.

Build this module through the monorepo root `build.ps1`. The combined
`build-release.ps1` packages its asset builder into the single overhaul wizard.
The code is GPL-3.0-only because it incorporates the GPLv3 Highly Theoretical
AICA emulation core; see `LICENSE.txt` and `THIRD_PARTY.md`.
