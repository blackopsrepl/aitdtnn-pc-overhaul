# AITD:TNN PC Overhaul

An independent preservation and compatibility overhaul for the **original PC
release** of *Alone in the Dark: The New Nightmare*—it is not based on and does
not support the Steam port. It keeps the original PC game, story, sound
effects, movies and game data intact while restoring selected Dreamcast-era
features and modernizing presentation and controller support.

Current integrated version: **0.3.0**.

## In-game preview

The screenshots below were captured from the supported PC build with the
overhaul active.

![Title screen](screenshots/title-screen.jpg?v=42ccccb)

![First playable scene](screenshots/first-scene.jpg?v=42ccccb)

![Inventory menu](screenshots/inventory-menu.jpg?v=42ccccb)

## What it adds

### Dreamcast music and ambience

The overhaul restores interactive music and ambience through the Dreamcast
release's Manatee/AICA synthesis path, using the PC engine's live gameplay
events. PC sound effects and native FMV audio continue to use their original
paths.

The installer extracts the required audio data locally from a Dreamcast disc
image that you provide. No Dreamcast audio, game, or other copyrighted assets
are included in this repository or in the installer.

### Correct 4:3 presentation on modern displays

- Borderless fullscreen on the primary monitor.
- Centered, proportional 4:3 output with black side pillars instead of
  stretching the game image.
- OpenGL 3.3 compatibility rendering for the original fixed-function game.
- Optional 4x MSAA, up to 16x anisotropic filtering, VSync, and fixes for
  common modern-driver edge-sampling issues.
- Native Bink movies remain responsible for decoding, timing, audio, and frame
  advancement while being presented inside the same proportional viewport as
  gameplay.

The default presentation includes a restrained neutral CRT-style treatment for
the game's low-resolution 640x480 signal: scanlines, a subtle aperture grille,
and mild bloom/halation. It does not add curvature, overscan, chromatic
aberration, vignette, aggressive sharpening, or stylized color grading.

Disable it by changing `renderer\aitd4-overhaul.ini`:

```ini
[CRT]
Enabled=0
```

### Restored character-selection movies

The renderer restores the verified post-interstitial character-selection movie
continuation for Aline and Carnby. Native character confirmation, portrait/title
voice, movie playback, and route setup occur in the intended order. A runtime
ledger records movie requests, opens, first frames, closes, and frame counts to
help diagnose unsupported or incomplete paths.

### Xbox controller support and rumble

The packaged Xidi controller layer maps an Xbox/XInput controller into the
legacy input interfaces expected by the PC game. The overhaul also restores the
game's retained Dreamcast-authored vibration path and sends its original weak
and strong profiles to the XInput motors.

Rumble can be adjusted in `rumble\aitd4-rumble.ini`:

```ini
[Rumble]
Enabled=1
Controller=Auto
Strength=1.0
Profile=Dreamcast
```

Rumble does not require a Dreamcast image at runtime.

## Requirements and compatibility

- 64-bit Windows.
- An installed English 15-slot/no-CD original PC release of *Alone in the
  Dark: The New Nightmare* (not the Steam port).
- Either owned Dreamcast disc image in a supported format (`.cue`, `.gdi`,
  `.iso`, `.bin`, `.img`, or `.raw`) for the music restoration.
- An OpenGL-capable display driver. An Xbox/XInput-compatible controller is
  optional.

The installer supports two verified English original-PC executable profiles:
the 15-slot/no-CD build and the retail CD build. Their SHA-256 values are:

```text
5668118e0e19d569986500a1c805a85397c8681e7b672b49a68645462eccc672
320908af4ce5c724b60a7eea6a5aade737d51d65aee8506744fce6e6dd0143e0
```

Address-sensitive modules fail closed when the executable is not one of these
supported builds. This protects other versions from receiving incompatible runtime hooks.

## Installation

1. Close the game.
2. Download and run `Setup.exe` from the [latest GitHub release](https://github.com/blackopsrepl/aitdtnn-pc-overhaul/releases/latest). Do not rely on the copy committed to this repository; it can lag behind the current release.
3. Select the folder containing `alone4.exe`.
4. Select either of your owned Dreamcast disc images when prompted.
5. Finish setup, then launch the game normally with `alone4.exe`.

The installer extracts the audio assets on your machine and installs the
validated loader, audio restoration, renderer, rumble module, renderer shaders,
and Xidi controller stack. It does not patch or replace `alone4.exe`, create a
launcher, or create a shortcut.

## FAQ

### Which PC versions work?

Version 0.3.0 supports the verified English 15-slot/no-CD executable and the
stock retail PC CD executable. The installer checks the executable's SHA-256
hash before installing address-sensitive modules, so other builds, repacks, and
storefront releases are rejected rather than patched unsafely. See the two
supported hashes in [Requirements and compatibility](#requirements-and-compatibility).

### Does this support the Steam version?

No. The overhaul was developed and tested against the original PC release; the
Steam port is not supported or currently tested.

### I have a supported executable, but setup says it is unsupported. What should I do?

First make sure you downloaded `Setup.exe` from the [latest GitHub release](https://github.com/blackopsrepl/aitdtnn-pc-overhaul/releases/latest), rather than using the repository copy. Then confirm that `alone4.exe` matches one of the hashes above. If it still fails, open an issue and include the installer message, the executable's SHA-256 hash, your game edition, and operating system.

### Do I need both Dreamcast discs?

No. Either Dreamcast disc image is sufficient: both contain the same audio
payload used by the installer. You must supply an image you own; the project
does not distribute Dreamcast game or audio assets.

### Does it work on Linux or Wine?

Linux/Wine compatibility has not been tested or supported yet. The current
requirements target 64-bit Windows, so Wine results are welcome as issue
reports but cannot be guaranteed.

### Why is a Dreamcast image needed, and will I need it every time I play?

The installer extracts the Dreamcast music and ambience from your image locally
so that no copyrighted Dreamcast assets need to be distributed. The image is
needed for installation only; rumble also does not require it at runtime.

### Save games

The original PC game expects a `Save` folder beside `alone4.exe`, but does not
always create it. If saving appears to succeed but Load Game shows no saves,
create this folder manually:

```text
<game folder>\Save\
```

Save files are stored there as `_Alone4_*.sav`.

## Safe uninstall and upgrades

Installation and uninstall are ownership-aware. Existing files from a previous
controller or overhaul setup are backed up and restored. If an installed file
has been modified, or an unknown file has been added to an overhaul directory,
uninstall preserves the current stack in a timestamped directory rather than
silently deleting it.

In-place integrated-version upgrades are intentionally refused. Uninstall the
existing integrated version first, then install the new version.

The loader writes diagnostics beside the game executable to:

```text
aitdtnn-overhaul-loader.log
```

Component logs are kept in their respective `audio-restoration`, `renderer`,
and `rumble` directories.

## Scope and known limits

This project is deliberately conservative. It does not provide widescreen
projection, AI texture upscaling, texture replacement, replacement movies,
stylized grading, fog redesign, or undocumented model/shadow changes. Music
follows the PC engine's live cue and event timing; it does not independently
replace every PC scene-to-cue decision with Dreamcast scheduling.

The current runtime has been manually validated through startup and the first
playable areas with Dreamcast synthesis/banks, native PC sound effects and FMV
audio, proportional rendering, CRT presentation, Xbox input, and rumble active
together. This is not a claim of complete full-game validation; remaining FMV
and presentation acceptance work is documented in [`REPORT.md`](REPORT.md).

The renderer's investigation and movie inventory are available in
[`renderer/docs/fmv-audit.md`](renderer/docs/fmv-audit.md), with presentation
design notes in [`renderer/docs/crt-design.md`](renderer/docs/crt-design.md).

## Building from source

Runtime modules are 32-bit x86 because the game is 32-bit. The asset builder,
release packaging scripts, and installer target 64-bit Windows. On a 64-bit
Windows machine with Visual Studio 2022 C++ tools:

```powershell
.\build.ps1
.\build-release.ps1
```

`build.ps1` builds and tests the maintained audio, renderer, rumble, and loader
sources. `build-release.ps1` validates the recorded working payload, packages
the asset builder, compiles the Inno Setup wizard, and creates the release
artifacts. Use `-Development` only when a test installer from an uncommitted
tree is required.

The destructive installer lifecycle matrix is available at
`installer\tests\Test-InstallerLifecycle.ps1`; it requires a user-supplied
supported game executable and Dreamcast image and runs in a disposable test
directory.

## License and credits

See [`LICENSE.txt`](LICENSE.txt), [`NOTICE.md`](NOTICE.md), and the component
license files for licensing and third-party notices. The audio restoration
includes the GPLv3-only Highly Theoretical AICA emulation core; Xidi is included
under its own license.
