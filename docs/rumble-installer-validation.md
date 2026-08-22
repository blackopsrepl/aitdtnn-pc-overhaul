# Rumble, installer and validation

## 8. Dreamcast-authored rumble restoration

### 8.1 Correcting the reverse-engineering target

The first rumble investigation instrumented Flycast's Puru Puru path and found
only repeated zero-power commands. A later static candidate, Dreamcast function
`0x8C0D3C04`, was also initially treated as vibration. Exact comparison against
the official Sega sound library proved it was `_sdSndSetFxPrg`, an AICA DSP
effects/reverb program selector. Script opcode 57 belonged to that acoustic path,
not rumble. Implementing it as vibration would have fabricated effects.

The useful breakthrough came from comparing homologous PC and Dreamcast game
code rather than trying to fix the emulator. The Windows executable retains the
game-authored vibration requests, duration state and tick scheduler. Only the
final platform methods were compiled as stubs.

### 8.2 Final hook surface

The rumble module restores three verified PC backend methods:

- `0x004A436C`: vibration enabled state;
- `0x004A4379`: selector/value submission;
- `0x004A439F`: backend availability.

The retained game scheduler calls the backend in the same order as Dreamcast:
selector 0 and then selector 1. Dreamcast `MDCF_SetCondition` replaces the active
command immediately, so the second call wins. The implementation preserves this
sequential replacement behavior rather than inventing a max, mix or priority
rule. Decoded output is sent equally to both XInput motors, matching the normal
Dreamcast-emulator gamepad interpretation.

Controller selection, loss, game vibration settings and process exit are handled
without modifying the existing input translation DLLs. The module needs neither
Flycast nor a Dreamcast image at install or runtime because the relevant authored
request logic already exists in the PC executable.

### 8.3 Complete Xbox input path

The validated installation uses Xidi 5.0.0 through both `dinput8.dll` and
`winmm.dll`. The latter is required because the PC executable reads its legacy
joystick axes through WinMM. `Xidi.ini` maps the Xbox controls to the game's
known keyboard actions while preserving continuous right-stick X/Y values for
the flashlight. The installer also supplies the validated 96-byte `keys.bin`.
All five controller files are transactionally backed up, hash-owned, restored on
uninstall and packaged byte-for-byte from the working installation.

## 9. Installer, ownership and rollback

The public package is one Inno Setup wizard. It asks for:

- the installed PC game directory;
- either owned Dreamcast disc image.

In one pass it verifies the PC executable, extracts the required audio assets to
a temporary directory, preserves any existing overhaul stack, installs the
exact validated loader, three modules, controller stack, configurations and
shaders, and verifies the final payload. It creates no desktop or Start Menu
shortcut and requires no user-facing launcher. The game starts normally through
`alone4.exe`.

The manager records SHA-256 ownership of installed files. It preserves a previous
`audio-restoration`, `renderer`, `rumble`, app-local `version.dll`, controller
wrappers, Xidi configuration and `keys.bin` before installation. Uninstall
restores that exact previous stack. If an installed file was later changed or an
unexpected file was added, uninstall moves the current tree to a timestamped
preservation directory instead of deleting it or overwriting later work.

Version 0.2.0 refuses an in-place upgrade when an integrated ownership manifest
already exists. The user must uninstall the current integrated version first.
This makes the rollback chain explicit and prevents a later setup from replacing
the metadata needed to restore the original pre-overhaul stack. Installer
metadata and documentation are included in the same hash inventory; there is no
blanket recursive uninstall deletion.

`alone4.exe` is never part of the ownership set because it is never modified.
Legacy PE patch/unpatch routines remain only as source-level development history
and tests; the public asset-builder command line exposes asset extraction only.

## 10. Validation

The renderer and rumble builds treat warnings as errors; the root build performs:

- native music resolver, initializer and MIDI lifecycle tests;
- public asset-builder CLI tests and legacy exact-unpatch safeguard tests;
- viewport calculations for common display shapes;
- carried-input and FMV neutral-release tests;
- character-selection ID mapping and carried-input neutral-release tests;
- CRT transfer, mask-energy and Bink-frame conversion tests;
- real OpenGL context, MSAA, anisotropy, state-restoration and compositor tests;
- dynamically loaded renderer tests for both Bink backend models, CRT disabled,
  alternate open fallback, custom logical resolution and rejected-scale
  fail-closed behavior;
- rumble command and replacement-order tests;
- loader SHA-256 vectors, x86 export/ordinal verification, dependency checks and
  exact executable-profile validation.

The active OpenGL Bink backend and Carnby's post-interstitial `SELECT_C` have
been manually runtime-tested through startup and the first playable areas with
Dreamcast synthesis/banks, PC sound effects and movie audio, proportional
rendering, the CRT pipeline, Xbox input and rumble active together. The retained
ledger records all 87 frames of `SELECT_C`. Aline and the alternate Bink buffer
backend currently have static or synthetic-harness coverage only. This is not a
full-game automated audio or FMV comparison.

The non-publishable development installer has passed a disposable test matrix:
fresh install from Disc 1 CUE/BIN input, exact executable-hash preservation,
pre-existing stack backup and normal restoration, refusal of an in-place upgrade
without changing payload or ownership, and preservation of a modified DLL plus
unknown game/app files while still restoring the previous stack. The temporary
test tree was removed afterward.
The reproducible harness is `installer/tests/Test-InstallerLifecycle.ps1`.

Release packaging does not rebuild or substitute the validated runtime DLLs.
Their exact hashes are recorded in `payload/working-payload.json`; the release
builder refuses to package a byte-different payload. Runtime source remains in
the monorepo and is built/tested separately by `build.ps1`.

Pending renderer acceptance covers Aline's restored movie in-game, a real
CRT-disabled run, alt-tab/focus/context recreation, scene/UI/inventory/fog/shadow
captures, repeated transitions, save/load cycles and clean exit.

## 11. Known limits

- Only the exact executable hash in section 2 is supported.
- The installer requires a user-supplied Dreamcast disc image for music assets.
- Music uses the PC engine's unchanged gameplay decisions and game-side DSEQ
  timing. DSEQ is not submitted as native Manatee SMSD, and a genuinely
  different scene-to-cue decision is not independently replaced.
- Exact loader-associated identity still requires a focused full-route gameplay
  acceptance run. A missing or contradictory association fails closed and is
  logged as `identity-missing` or `validation-failed`; there is no guessed alias
  or PC-music fallback.
- The renderer does not provide replacement movies. Community packs can contain
  platform-content and audio differences.
- The FMV request ledger covers the common runtime surface, but every reachable
  story event on both character routes has not yet been played to completion in
  one published validation run.
- Fog, projection-dependent model placement and shadow behavior are left native
  unless a specific, reproducible defect is proven.
- The CRT path is tuned for restrained low-resolution presentation, not for exact
  emulation of one undocumented consumer television.

## 12. Reproducibility and publication checklist

From a clean source tree on 64-bit Windows, `build.ps1` uses the Visual Studio
2022 C++ tools to rebuild and test the maintained runtime sources. Release
packaging uses 64-bit CPython 3.10.2 and Inno Setup 6:

```powershell
.\build.ps1
.\build-release.ps1
```

The publication build requires a clean committed tree, verifies every frozen
runtime file against `payload/working-payload.json`, uses pinned asset-builder
dependencies, creates the single setup executable and writes an exact
corresponding-source archive plus `SHA256SUMS.txt`. It does not substitute newly
compiled runtime DLLs for the working payload. `-Development` is available for
an installer test from an uncommitted tree.

Before publication:

1. require a clean diff audit and `git diff --check`;
2. confirm no game or Dreamcast assets are tracked;
3. run the complete source build from a clean clone;
4. compile the combined setup from pinned dependencies;
5. run the installer payload-hash and ownership-aware uninstall test;
6. record the release installer hash and exact tested executable/Bink profiles;
7. publish only the source, notices, licenses, setup executable and checksum.

The project is independent and unaffiliated with the game's, platform's or
middleware's rights holders. See `NOTICE.md`, `LICENSE.txt` and component
third-party notices for licensing details.
