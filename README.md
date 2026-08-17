# AITD:TNN PC Overhaul

Current integrated version: `0.2.0`. The exact supported installation has been
runtime-validated with Dreamcast music and ambience, PC SFX and FMV audio,
proportional borderless 4:3 rendering, Xbox controls and restored rumble active
together. This is still an unpublished working tree, not a public release.
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
all three modules and the shared loader in one transaction, and creates no shortcut
or user-facing launcher. `alone4.exe` remains byte-for-byte unchanged.

Install and uninstall are ownership-aware. Setup preserves any previous
`audio-restoration`, `renderer`, `rumble`, or app-local `version.dll` stack. Uninstall
restores that stack. If an installed overhaul file was later modified or another
file was added to its module directories, uninstall moves the current stack to a
timestamped preservation directory instead of deleting or overwriting it.

Run `build.cmd` or `powershell -File build.ps1` from a Visual Studio 2022 C++
build machine to build and test the audio, renderer, rumble, and loader modules. Run
`powershell -File build-release.ps1` to package the asset builder and compile the
single Windows Setup wizard.

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

Production PE builds use reproducible compiler/linker output. Component manifests
are regenerated and hash-verified by the root build. The repository remains
uncommitted and unpublished pending an explicit release decision.
