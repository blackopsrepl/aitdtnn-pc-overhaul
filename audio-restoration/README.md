# AITD:TNN PC Dreamcast music restoration

This module makes the Windows game render interactive music and ambience through
the Dreamcast release's Manatee driver, program/sample banks and AICA synthesis.
The original PC music renderer does not produce the resulting audio.

Responsibility is divided at the same game/backend boundary used by the runtime:

1. The PC game's unchanged scene logic selects one or more music containers.
2. Its existing game-side DSEQ parser produces timed program, note, controller
   and pitch events. DSEQ parsing is game logic, not audio synthesis.
3. The hook identifies the exact container and suppresses the corresponding PC
   music dispatch.
4. Those events drive the extracted Dreamcast `MANATEE.DRV`, program/sample
   banks and emulated AICA.
5. The generated PCM is submitted to the game's existing Miles digital driver.

Dreamcast DSEQ is not Manatee SMSD/SMSB data and cannot be made into a native
Manatee sequence bank by changing its magic or adding a wrapper. Extracted DSEQ
payloads are used to establish container identity; the module does not replace
the game's parser or independently recreate scene scheduling. A development
comparison found the relevant PC and Dreamcast DSEQ payloads and associated
maps/banks byte-identical. Consequently, retaining the game's parser preserves
the authored event stream while replacing the sound-producing backend.

PC sound effects and native FMV audio remain on their original paths.

The AICA core is output-only: it never opens WinMM or a second audio device.
The hook captures the game's actual Miles `HDIGDRIVER` through its imported
`AIL_waveOutOpen` and `AIL_allocate_sample_handle` calls, allocates a dedicated
Miles sample, and continuously submits two locked 44.1 kHz stereo PCM buffers.
Persistent PC music dispatch is suppressed from the first hooked event; there
is no PC-music fallback. Short MIDI sound effects and FMV audio are unchanged.
There is one authoritative identity path: the hook captures the container name
at the game's own MIDI-container loader. It records each created DSEQ object
against that sequence container and separately records the container most
recently installed in each shared bank slot. At dispatch, complete DSEQ data
validates the sequence identity and complete live maps validate the current bank
identity. This preserves room transitions where an existing sequence remains
active after the shared program/sample bank changes. Each bank-load generation
also invalidates prior player bindings, so active sequences reconnect even when
their own pointers and selectors remain unchanged. The runtime does not guess,
alias, or fall back.

The full 69-container catalog has six exact map collisions. Three bank-0 pairs
are distinguishable by DSEQ, two pairs have byte-identical DSEQ payloads, and
one pair shares one payload but has distinct alternatives. The load association
preserves their actual filenames even when every content-based identifier is
identical. See the report's complete collision audit for the named pairs and
retained-log findings.

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

Runtime assets and diagnostics live under `audio-restoration`. Loader-associated
identity disambiguates containers with identical content; full DSEQ validates
the sequence object and full map comparison validates the independently current
shared bank. Music-slot replacement explicitly tears down active notes and
refuses to render an unresolved persistent music bank.

Build this module through the monorepo root `build.ps1`. The combined
`build-release.ps1` packages its asset builder into the single overhaul wizard.
The code is GPL-3.0-only because it incorporates the GPLv3 Highly Theoretical
AICA emulation core; see `LICENSE.txt` and `THIRD_PARTY.md`.
