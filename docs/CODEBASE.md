# Codebase guide

This is the best starting point for reading the overhaul's source. It assumes
you understand ordinary programming ideas but not C++, Windows DLL injection,
OpenGL, or the original game.

## The system in one minute

The installed game still runs its original `alone4.exe`. The overhaul adds an
app-local `version.dll`, a standard Windows DLL name that the game already asks
Windows to load. This proxy forwards the real Windows Version API and pauses the
game at its entrypoint long enough to validate the executable and initialize
three modules:

1. audio restoration replaces only PC music synthesis with the Dreamcast sound
   driver, banks and emulated AICA;
2. the renderer intercepts the game's OpenGL and Bink presentation calls;
3. rumble connects retained Dreamcast vibration requests to XInput.

Only after all three report success does the proxy let the game start. This is
called *fail closed*: a half-loaded overhaul cannot silently run.

## A tiny C++ reading primer

- A `.hpp` file describes data or functions that several source files can use.
- A `.cpp` file is compiled. A group of compiled files is linked into a DLL or
  test executable.
- This project also uses `.inc` fragments for the large hooks. They are included
  by one small `.cpp` map in a documented order and compile as one unit. That
  keeps shared hook state and exact function addresses unchanged without putting
  thousands of lines in one navigation-hostile file.
- `namespace` groups related names. An unnamed `namespace { ... }` makes details
  private to one compiled unit.
- A function pointer stores the address of a function. Hooks save the original
  function pointer and route selected calls through a replacement function.
- `extern "C"` gives an exported initializer a stable name that another DLL can
  find. `WINAPI`, `__cdecl`, and `__fastcall` describe how 32-bit arguments are
  passed; they are part of compatibility, not business logic.
- `HRESULT`, `DWORD`, `HWND`, and similar uppercase types come from the Windows
  API. OpenGL types usually begin with `GL`.
- `std::` names come from the C++ standard library. `std::vector`, for example,
  is a growable array whose memory is managed automatically.

## Runtime start-up path

Read these files in order:

1. `shared/loader/src/version_proxy.cpp` is the map for proxy forwarding,
   executable validation and module startup.
2. `audio-restoration/src/audio_hook.cpp` maps the scene identity and music
   dispatch pipeline.
3. `renderer/src/hook_main.cpp` enters the renderer; `graphics_hook.cpp` maps
   all window, OpenGL and Bink interception.
4. `rumble/src/hook_main.cpp` maps configuration, the timed worker and the
   verified game hooks.

Every map lists responsibility-named fragments from declarations through public
entrypoints. Read them top to bottom. A fragment may use a name from an earlier
fragment because the compiler sees the combined text.

## Audio restoration

The PC game remains the sequencer. Its scene code loads a MIDI container and its
existing DSEQ parser emits timed note, program, controller and pitch events. The
hook records the exact container associated with each created sequence, blocks
that sequence's PC music dispatch, and passes the events to the Dreamcast
backend. Sound effects and movie audio never enter this path.

`audio_hook_catalog.inc` reads the locally extracted catalog. `audio_hook_identity.inc`
tracks two facts that must not be conflated: the container that owns a sequence
and the container currently loaded into a shared bank slot. `audio_hook_dispatch.inc`
reconciles those facts for each player. `audio_hook_lifecycle.inc` starts the
renderer and installs hooks.

`audio_renderer.cpp` owns the emulated Dreamcast driver/AICA and generates PCM.
`miles_stream.cpp` submits that PCM through the game's existing Miles digital
driver. `tools/build_assets.py` is only the command-line front door; its
`asset_builder/` modules separately handle PE recovery history, disc filesystems,
disc formats and runtime catalog construction.

## Renderer and movies

The renderer does not rewrite the game's renderer. It intercepts the small API
surface where the old engine meets Windows, OpenGL and Bink. Gameplay first
lands in an off-screen 4:3 framebuffer. The final pass resolves MSAA, applies
the optional neutral CRT pipeline, and presents a centered proportional image.

The `graphics_hook.cpp` include map is the table of contents. Window/input and
movie fragments deal with engine-facing behavior. Context, CRT, framebuffer,
legacy-GL and presentation fragments deal with graphics behavior. The routing
fragment connects the original imported APIs to the replacements.

`runtime.cpp` loads configuration and exposes diagnostic state. Small headers
such as `viewport.hpp`, `movie_skip_gate.hpp` and `bink_frame.hpp` isolate pure
rules that can be tested without launching the game.

## Rumble

The PC executable retained the Dreamcast vibration scheduler but its platform
backend was disabled. `rumble_runtime.inc` validates the executable and opens
XInput. `rumble_worker.inc` translates authored selector/value commands and
stops motors at their deadline. `rumble_hooks.inc` connects those operations to
the verified addresses and rolls back partial installation on failure.

`rumble_protocol.hpp` contains the platform-independent conversion rules. Its
unit test is the safest place to begin if you want to understand the command
format.

## Installer and ownership

`installer/AITDTNN-PC-Overhaul.iss` declares files and installer metadata;
`AITDTNN-PC-Overhaul.Code.iss` implements wizard behavior. The installer runs
`Manage-Overhaul.ps1`, whose `Core` companion contains path confinement,
hashing, ownership and restoration helpers.

Installation is transactional. Existing files are first moved to a transaction
backup. Finalization records the hash of every owned file. Uninstall removes
files only when they still match that manifest; changed files are preserved
instead of guessed away, then the previous stack is restored.

## Building and checking changes

Run `build.ps1` from the repository root. It builds all 32-bit native modules,
runs their tests, verifies exports and payload hashes, parses PowerShell, and
enforces the source-size rule. Run `build-release.ps1 -Development` for a local
installer test. Publication builds intentionally require a clean committed tree.

`tools/Verify-SourceLayout.ps1` rejects a first-party code or documentation file
at 300 physical lines or more. Imported source, generated output and license
texts are excluded because editing or splitting them would damage provenance.

The destructive installer lifecycle test uses a disposable directory and
requires paths to a supported `alone4.exe`, the built Setup program and an owned
Dreamcast image. It verifies install, refusal of unsafe upgrade, uninstall,
rollback and preservation behavior.

## Where to go deeper

- `REPORT.md` is the technical-report index.
- `audio-architecture.md` records the audio evidence and identity model.
- `renderer-architecture.md` records renderer, FMV and CRT decisions.
- `rumble-installer-validation.md` records rumble, ownership and release checks.
- Each component README states its public contract and supported executable.

When changing a hook, update its fragment header and the relevant component
README. When changing behavior visible to users or packagers, also update the
root README, technical report and changelog.
