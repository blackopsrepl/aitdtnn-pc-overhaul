# AITD:TNN PC Dreamcast music restoration

This module makes the Windows game render interactive music and ambience through
the Dreamcast release's Manatee driver, program maps, sample banks and AICA
synthesis. The equivalent live sequence events are taken from the PC engine;
the audited PC and Dreamcast sequence payloads are byte-identical. PC sound
effects and native FMV audio remain on their original paths.

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
