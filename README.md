# AITD:TNN PC Overhaul

Run `Setup.exe` from the repository root. Select the supported installed PC game
folder and either Dreamcast disc image. The wizard installs the complete working
overhaul in one pass; afterward, start the game normally with `alone4.exe`.

Current integrated version: `0.2.0`. The exact supported installation has been
manually runtime-validated through startup and the first playable areas with the
Dreamcast synthesis/bank path, PC SFX and FMV audio, proportional borderless 4:3
rendering, Xbox controls and restored rumble active together. This is still an
unpublished working tree, not a public release or a full-game validation claim.
This monorepo is the canonical source for the integrated stack; earlier
standalone renderer and music workspaces are reference/development inputs, not
the release source of truth.

This is the collision-free monorepo for the modular *Alone in the Dark: The New
Nightmare* PC overhaul. It contains no game data and no Dreamcast assets.

The shared loader is an app-local, 32-bit `version.dll` proxy. The original game
already imports `VERSION.dll`, so normal `alone4.exe` launch loads the proxy with
no launcher and no on-disk executable patch. Before the original entrypoint runs,
the proxy validates the supported pristine executable and initializes modules in
this fixed order:

1. `audio-restoration\aitd4-audio-hook.dll` via
   `DWORD WINAPI AITD4_AudioInitialize(void*)`
2. `renderer\aitd4-renderer-hook.dll` via
   `DWORD WINAPI AITD4_Initialize(void*)`
3. `rumble\aitd4-rumble-hook.dll` via
   `DWORD WINAPI AITD4_RumbleInitialize(void*)`

The loader fails closed on any validation, load, export, or initialization error.
It appends diagnostics to `aitdtnn-overhaul-loader.log` beside `alone4.exe`.

The combined Inno Setup wizard asks for the installed PC game and either owned
Dreamcast disc image. It extracts the required audio assets locally, installs
the exact validated loader, all three modules, renderer configuration/shaders
and complete Xidi Xbox-controller stack in one transaction, and creates no
shortcut or user-facing launcher. `alone4.exe` remains byte-for-byte unchanged.

Install and uninstall are ownership-aware. Setup preserves any previous
`audio-restoration`, `renderer`, `rumble`, app-local `version.dll`, Xidi wrappers,
Xidi configuration and `keys.bin`. Uninstall restores that stack. If an installed
overhaul file was later modified or another file was added to its module
directories, uninstall moves the current stack to a timestamped preservation
directory instead of deleting or overwriting it.
Integrated-version upgrades are deliberately refused; uninstall the existing
integrated version first so its original rollback ownership remains unambiguous.

Run `build.cmd` or `powershell -File build.ps1` from a 64-bit Windows Visual
Studio 2022 C++ build machine to build and test the audio, renderer, rumble and
loader sources. Release packaging deliberately uses the byte-exact working
runtime recorded in `payload\working-payload.json`; it does not replace those
validated DLLs with a fresh rebuild. Run `powershell -File build-release.ps1`
from a clean committed tree to validate that payload, package the asset builder,
compile the single Windows Setup wizard and create its exact corresponding-source
archive. `-Development` permits a test installer from an uncommitted tree.

The packaged asset builder and combined installer target 64-bit Windows; the
game, loader and runtime hook DLLs remain 32-bit x86.

`installer\tests\Test-InstallerLifecycle.ps1` performs the destructive-edge
installer matrix in a validated disposable directory: fresh install, upgrade
refusal, exact previous-stack restoration and modified/unknown-file
preservation. It requires a user-supplied supported executable and Dreamcast
image and deletes its scratch tree unless `-Keep` is specified.

The rumble module restores the PC port's retained Dreamcast-authored request path
at its dead backend and outputs the original stop, weak, and strong profiles over
XInput. It requires no Dreamcast image at install or runtime.

The renderer restores the omitted character-selection movie request at the
verified post-interstitial continuation: character confirmation and the native
portrait/title voice complete first, then native Bink plays `SELECT_A` or
`SELECT_C`, then route setup continues. The common movie controller also keeps a
runtime ledger of expected requests, opens, first frames, closes and frame counts.
The physical movie and trigger inventory is maintained in
`renderer\docs\fmv-audit.md`; unresolved script-expression IDs remain explicitly
documented instead of being guessed.

The renderer also provides an optional neutral CRT presentation path, enabled
by default for the current low-resolution asset stack. Gameplay and decoded
Bink frames share the same 640x480 signal reconstruction, linear-light aperture
grille/scanline response and restrained neutral bloom/halation. The physical
4:3 viewport and black pillars are unchanged; native Bink still owns decode,
audio, timing and frame advancement. Setting `[CRT] Enabled=0` restores the
previous compositor and native Bink blit path.

Production PE builds use reproducible compiler/linker output. Component manifests
are regenerated and hash-verified by the root source build. The distributable
`Setup.exe` packages the exact validated runtime recorded in
`payload\working-payload.json`.
