# AITD:TNN PC Overhaul: technical report

## 1. Purpose and scope

This project is an independent compatibility and preservation overhaul for the
Windows release of *Alone in the Dark: The New Nightmare*. Its purpose is to
keep the PC version's native game logic, sound effects, movie playback and data
while correcting specific omissions or degraded platform backends with evidence
from the Dreamcast release.

The integrated stack has three independent runtime modules:

1. Dreamcast music and ambience rendering with PC sound effects and movie audio
   left native;
2. proportional 4:3 rendering, presentation restoration and FMV corrections;
3. restoration of the game's retained Dreamcast-authored vibration path through
   XInput.

The public installer contains no executable, disc image, movie, music sequence,
sample bank, script, texture or other game asset. The user supplies an installed
supported PC game and either owned Dreamcast disc image. Required Dreamcast audio
data is extracted locally during installation.

This report documents the investigation, failed assumptions, final architecture,
evidence limits and validation requirements. It deliberately distinguishes a
verified result from a plausible but unverified one.

## 2. Supported baseline

The current release supports exactly three English `alone4.exe` builds:

| Profile | SHA-256 | Hook layout |
|---|---|---|
| Original 15-slot/no-CD | `5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672` | 15-slot |
| Original retail CD | `320908AF4CE5C724B60A7EEA6A5AADE737D51D65AEE8506744FCE6E6DD0143E0` | retail CD |
| Steam (app 259190) | `228123A2E5620AD1976F83B867513B761CA824610E0BF73678C5BECEBA6BD815` | retail CD |

The Steam executable is the retail CD image with 32 bytes changed: the `je` after
the CD-file `CreateFileA` at RVA `0x8D722` becomes `nop nop`, the `jne` on the
cached CD drive letter at `0x8DB28` becomes `jmp`, and the "Please insert CD 3"
string at `0xC708C` becomes "Loading, please wait...". None of those bytes lie
in a hooked or verified interval, so Steam reuses every retail CD address.
`tools/Verify-ExecutableProfiles.ps1 -SteamExe` re-proves this byte-for-byte.
Steam launches `alone4.exe` directly, so the loader gate sees the real process.

| Item | Value |
|---|---|
| On-disk executable modification | None |
| Runtime architecture | 32-bit x86 |
| Reference display used for acceptance | 1920x1080, centered 1440x1080 4:3 viewport |
| Tested local Bink build | Bink 1.5s, SHA-256 `E3D19E66B6135925116C8E7585B6E6E3746E50D79F1EC863CDA615B0189DF1A9` |
| Tested Steam Bink build | RAD 1998-1999 `binkw32.dll`, SHA-256 `7EF2F1D2B6D833BCC3BEA2359975E747B84E166BEBFDF92E6A2A7389B8F2C0E6`; exports every hooked and imported name |

Address-sensitive modules hash the executable and validate the expected bytes at
every patch site. An unknown executable or a partially compatible build is
rejected. This narrow support policy is preferable to silently applying hooks to
the wrong code.

The overhaul includes the exact signed 32-bit Xidi 5.0.0 controller layer and
configuration used by the validated installation. It does not distribute
replacement movies; any community movie pack remains a local installation
choice.

## 3. Integrated loading architecture

Early prototypes modified the PE entrypoint and added an `.aitdaud` section to
load the music DLL. That proved the hook could start, but it was unsuitable for a
modular stack: later modules could also need executable changes, independent
uninstall could not safely restore an executable modified by another tool, and
the altered entrypoint became a collision point.

The final architecture leaves `alone4.exe` byte-for-byte untouched. The game
already imports the system Version API, so an app-local 32-bit `version.dll`
proxy is loaded through normal Windows DLL resolution. It forwards all 17
expected Version exports and ordinals to the system DLL. Before replaying the
original entrypoint bytes, it validates the executable and initializes:

1. `audio-restoration\aitd4-audio-hook.dll`;
2. `renderer\aitd4-renderer-hook.dll`;
3. `rumble\aitd4-rumble-hook.dll`.

Each module exposes one common-form explicit initializer:

```cpp
DWORD WINAPI ModuleInitialize(void* reserved);
```

The loader resumes the original entrypoint only after all three initializers
return success. Missing DLLs, wrong exports, unsupported binaries and partial
initialization are fatal. This removes launcher timing races and prevents a game
session from continuing with only part of the stack active.

Production modules use the static Microsoft runtime (`/MT`). The loader itself
depends only on system `KERNEL32.DLL` and `USER32.DLL`.


## Detailed subsystem reports

The remainder of the investigation is split by responsibility so readers can
open only the subsystem they are trying to understand:

- [Audio restoration](docs/audio-architecture.md) explains disc extraction, scene identity, sequence dispatch and the Dreamcast sound core.
- [Renderer, FMV and CRT](docs/renderer-architecture.md) explains the OpenGL interception and presentation pipeline.
- [Rumble, installer and validation](docs/rumble-installer-validation.md) explains Dreamcast vibration translation, safe ownership and release checks.
