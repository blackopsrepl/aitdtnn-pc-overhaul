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

The current release profile intentionally supports one executable:

| Item | Supported value |
|---|---|
| PC executable | English 15-slot/no-CD `alone4.exe` |
| SHA-256 | `5668118E0E19D569986500A1C805A85397C8681E7B672B49A68645462ECCC672` |
| On-disk executable modification | None |
| Runtime architecture | 32-bit x86 |
| Reference display used for acceptance | 1920x1080, centered 1440x1080 4:3 viewport |
| Tested local Bink build | Bink 1.5s, SHA-256 `E3D19E66B6135925116C8E7585B6E6E3746E50D79F1EC863CDA615B0189DF1A9` |

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

## 4. Music and ambience restoration

### 4.1 Initial problem

The PC release does not reproduce the Dreamcast soundtrack correctly on modern
systems. The relevant content is not a set of ordinary streamed tracks: it is an
interactive MIDI-like sequence system with per-container program maps, sample
banks and scene state. A simple replacement of loose files or pre-rendered audio
cannot preserve transitions and authored interactivity.

The first investigation overemphasized the loose `Sound2` directory and assumed
that matching filenames or hashes would be enough. Extraction of the retail PC
data instead exposed 233 per-scene program-bank records representing 209 distinct
banks. That made a blanket sample replacement unsafe: the runtime container,
program map and sequence identity all matter.

### 4.2 What comparison established

A local PC-versus-Dreamcast development audit was performed for all 69 Aline and
69 Carnby music containers. After decompression and parsing, that audit reported
the following byte-identical between the compared releases:

- MIDB bank identifiers;
- program maps;
- sequence names, order and payloads;
- SMPB sample-bank payloads.

This corrected the working hypothesis that the loose PC MIDI assets themselves
were generally swapped. The compared copyrighted corpora are not retained in
the repository, and the one-off comparison has not yet been converted into a
BYO-data reproducibility command. It is therefore development evidence, not an
independently reproducible source-tree proof. The remaining observed failure
surface was runtime selection and rendering: which container the PC engine had
loaded, how ambiguous containers were identified, and how sequence events were
translated into synthesis.

#### DSEQ is not Manatee SMSD

The similar names and event-like payload initially suggested that the extracted
game-level DSEQ data could be wrapped in a Manatee SMSB and handed to the
driver's native sequence-start command. Direct validation against the retail ARM
driver disproved that assumption. Its native sequencer requires an SMSB v2 bank
containing SMSD tracks; the first SMSD words are consumed as native timing
fields. The corresponding DSEQ bytes instead contain game-specific metadata and
cannot be reinterpreted as those fields.

Therefore the correct boundary is not a new SMSB converter or a replacement
sequencer. DSEQ remains on the game side of the boundary, where the existing
game parser turns it into timed events. Manatee/AICA remains on the audio-backend
side, where those events select voices and produce PCM.

Six map-identity collision groups demonstrated why inference cannot be the
authoritative solution. Some containers have identical program maps, and some
also have identical sequence and bank payloads. The runtime now has one identity
source: the game function that loads a named MIDI container. The hook associates
that exact name with every sequence object created by the load and with the
shared bank slot updated by the load. Complete loaded DSEQ validates the former;
complete live program maps validates the latter. The identities are deliberately
separate because the game can keep a DSEQ player active while a room transition
replaces its shared program/sample bank.

If a persistent music bank cannot be resolved exactly, the hook refuses to bind
it instead of silently rendering through a generic bank.

#### Complete collision audit

An audit of all 69 extracted containers, using the runtime resolver's complete
key (`bank_id` plus every 128-byte program map), found exactly six collision
groups:

| Containers | Dreamcast bank | Live DSEQ evidence | Authoritative identity |
| --- | --- | --- | --- |
| `airc0`, `airv0` | byte-identical | distinct | captured load name |
| `jardinc0`, `jardinv0` | byte-identical | distinct | captured load name |
| `manoira0`, `manoirb0` | byte-identical | distinct | captured load name |
| `act_c12`, `act_c13` | byte-identical | all three payloads byte-identical | captured load name |
| `inv_b1`, `jarding1` | byte-identical | all three payloads byte-identical | captured load name |
| `chapelle`, `jardin1` | byte-identical | one shared payload | captured load name |

This is not a PC-versus-Dreamcast catalog difference: the compared platform
data is byte-identical, and 57 of the 69 containers are map-unique. More
importantly, every colliding group has both identical maps and a byte-identical
Dreamcast MPB bank. Content inference therefore cannot recover the authored
name for every case. Capturing the name and resulting object in the same loader
call removes that collision instead of concealing it behind an equivalent alias.

The retained logs contain one unresolved dispatch: slot 3, bank 0, with two
candidates during the transition from `intro_b1` into the three resolved
`jardin2` layers. The obsolete `CreateFileA` heuristic installed but captured no
associated loads, while its DSEQ probe compared the wrong offsets. Both
inference mechanisms were removed. The replacement records identity around the
game's synchronous container parser and validates the full DSEQ after accounting
for the parser's in-place big-endian-to-little-endian offset conversion. The old
log did not record candidate names or the suppressed event bytes, so it cannot
prove which bank-0 pair or how audible that event would have been.

The replacement was then exercised through the same title/menu transition. The
game loaded `\midi\carnby\jardinv0`; the hook associated that name with slot 3's
exact base/table pointers and validated the complete `vent_v` DSEQ. The result
was `match=jardinv0/vent_v evidence=loader-identity candidates=1 bound=yes`.
All three simultaneously loaded `jardin2` layers were independently registered
and validated, with no unresolved, missing-identity or validation-failure entry.
This validates the formerly unresolved position without claiming that the test
entered Carnby gameplay.

A subsequent Aline run exposed the sequence/bank lifetime distinction. On the
`grenier1` to `grenier5` transition, slot 0 retained and continued dispatching
its exact `grenier1/intro_a1` DSEQ while bank 1 and its complete live maps changed
to `grenier5`. The executable's update loop at `0x498877` processes a player only
while its `state+0x134` active flag is set, and its DSEQ parser applies the
current inverse program map before calling the hooked dispatcher. The slot was
therefore neither stale nor unresolved: it was an active `grenier1` sequence
driving the current `grenier5` bank. Treating sequence container and bank
container as one identity caused 213 valid dispatches to be suppressed. The
resolver now validates and tracks those identities independently.

Repeated transitions then exposed a second-order lifetime issue: loading a new
container into a shared Manatee bank invalidates every renderer player attached
to that bank, even if a player's PC sequence pointers and selector do not change.
The hook previously used only those per-player fields to decide whether to bind
again. It now includes the authoritative bank-load serial in that state key, so
every affected active player is rebound after every shared-bank replacement.

### 4.3 Continuous-tone failures

Several prototypes produced unintelligible output or one sustained tone. Logs
and offline event replay showed that this was not simply a volume problem and
not adequately explained by a missing PC note-off. The implementation had
multiple hazards:

- unresolved persistent music could fall through to the `gamesnd` bank;
- scene replacement did not guarantee a complete active-note teardown;
- repeated note-ons were represented as a single active bit, so one note-off
  could not balance multiple starts;
- a sequence diagnostic compared the sequence base rather than the actual table
  at `base + 0x10`, making useful identity evidence appear absent;
- initialization performed too much work in `DllMain` and raced renderer
  readiness.

The final path uses counted note ownership, emits explicit teardown on every
scene-state replacement, initializes unknown programs to an invalid sentinel,
suppresses unsupported messages, and fails closed on unresolved persistent
music. Initialization moved to the explicit loader-controlled export and returns
success only after the Dreamcast renderer is ready.

### 4.4 Final audio division of responsibility

The PC engine remains responsible for gameplay-driven container selection and
its game-side DSEQ parser remains responsible for producing timed interactive
events. This is scheduling, not PC audio rendering. The hook suppresses the PC
music destination, validates the active DSEQ's exact source container, binds the
events to the independently current shared Dreamcast bank, and renders them
through the Dreamcast Manatee/AICA path based on the GPLv3 Highly Theoretical
core. Extracted Dreamcast DSEQ data supplies identity evidence rather than being
misrepresented as native Manatee SMSD.

The resulting music is therefore Dreamcast-driver/bank/AICA synthesis under the
PC executable's unchanged gameplay and DSEQ timing logic. It is neither the PC
music renderer nor an independently recreated Dreamcast scene system. The local
cross-platform comparison found the relevant DSEQ payloads, program maps and
sample banks byte-identical; retaining the existing parser avoids duplicating
that logic. The module would not correct a genuinely different scene-to-cue
decision. That boundary is explicit rather than hidden behind the word
"restoration."

The normalized local extraction result contains 69 scene containers, 70 bank
files including `gamesnd`, 161 DSEQ files and 550 program-map entries. Disc 1
and Disc 2 inputs normalize to the same runtime payload; no extracted file is
committed or packaged with the project.

The division is intentional:

- music and ambience synthesis: Dreamcast Manatee, banks and emulated AICA;
- music-container selection and DSEQ event timing: unchanged game logic;
- ordinary PC sound effects: native PC path;
- movie audio: native Bink path;

The module does not play MP3 replacements and does not ship Dreamcast assets.

## 5. Renderer and proportional presentation

### 5.1 Display contract

The renderer's locked default is exact 4:3, not widescreen projection. On a
1920x1080 monitor the game occupies a centered 1440x1080 viewport with 240-pixel
black pillars on each side. Models, prerendered backgrounds, masks, UI, maps and
movies retain the same proportions.

The game receives a compatibility OpenGL 3.3 context. Fixed-function behavior is
retained while framebuffer objects, multisample resolve, shaders and explicit
presentation become available. The game renders to RGBA8 with 24-bit depth and
8-bit stencil, normally with four-sample MSAA. The compositor preserves and
restores fixed-function and game-facing state, including hostile incoming masks,
polygon mode, logic operations and draw-buffer state, then explicitly rebinds
its owned framebuffer objects.

The implemented conservative restoration includes:

- VSync enabled by default;
- up to 16x anisotropic filtering for mipmapped textures;
- original filtering for non-mipmapped backgrounds, masks, UI and video;
- correction of applicable legacy `GL_CLAMP` use to `GL_CLAMP_TO_EDGE`;
- restrained gradient debanding and sub-LSB dithering;
- consistent 32-bit color and 24/8 depth-stencil behavior.

No widescreen projection, texture replacement, AI upscaling, sharpening,
cinematic grading, fog redesign or speculative model placement is included.
Those exclusions prevent an unverified artistic change from being described as
a restoration.

### 5.2 Borderless fullscreen and coordinate virtualization

Exclusive desktop mode changes are suppressed. The physical window covers the
primary monitor without decorations while the game sees the proportional
logical dimensions. Viewport, scissor and final output calculations are kept
separate so a custom logical 4:3 size still maps to the largest physical 4:3
area rather than becoming undersized or off-center.

The same distinction matters for movies: Bink presents to the physical window,
not the logical OpenGL framebuffer in every backend. Early code incorrectly used
logical render dimensions for Bink scaling, which could zoom, clip or miscenter
the title and movie image. The final mapping uses the physical 4:3 output
viewport.

## 6. FMV investigation and restoration

### 6.1 Why low-level Bink hooks were not enough

Initial FMV debugging focused on `BinkOpen`, scaling and blitting. That was
necessary for presentation correctness but could not explain an authored movie
that never reached Bink. The runtime ledger proved the post-character-selection
failure occurred earlier: confirmation completed, no movie request followed,
and therefore no `BinkOpen` could occur.

The supported executable has one common native movie controller at `0x004812CF`.
Script opcode 42, startup logos, intro, trailer, ending and direct UI callers all
converge on it. The primary and alternate `BinkOpen` callsites are merely two
path attempts inside the same native player.

### 6.2 Character-selection ordering

The first restoration inserted `SELECT_A` or `SELECT_C` too early, immediately
after confirmation. That was visibly wrong because the native portrait and the
spoken title interstitial must finish first.

PC and Dreamcast control-flow comparison established the exact order:

1. the player confirms item `0x2001` or `0x2002`;
2. the frontend completes its state-3, `0x80`-tick portrait/title/voice
   interstitial;
3. the higher new-game controller returns on a genuine new-game path;
4. the Dreamcast build requests movie ID 5 for Carnby or ID 4 for Aline;
5. route setup continues.

The PC cold continuation at `0x004BAE94` is the corresponding post-interstitial
position. The renderer replaces only that call with a helper that clears the
carried Action edge, invokes the existing native movie controller with the
Dreamcast-authored zero skip mask, performs the displaced original call and
returns its result unchanged. Load-game and cancelled paths do not execute the
restoration.

The character mapping is independently supported by the PC route strings and
the Dreamcast state byte: zero is Aline/ID 4 `SELECT_A`; nonzero is Carnby/ID 5
`SELECT_C`.

### 6.3 Skip input and lifecycle ledger

The native movie loop treats a newly pressed Action input as a skip. Controller
translation or focus reacquisition can make the confirmation press appear as a
fresh edge at movie start. A shared neutral-release gate suppresses Action and
Enter until the held state has first been observed neutral. A later release and
fresh press still skips normally.

Every request through the common controller is logged as:

`expected -> request -> open -> first frame -> close -> frame count -> complete`

This distinguishes:

- an expected movie never requested by game logic;
- a request whose file failed to open;
- a movie opened and immediately skipped;
- a partially played movie;
- a completed movie.

The full 55-entry PC catalog, the Dreamcast disc inventory, direct callers and
14 serialized opcode-42 nodes are recorded in
`renderer/docs/fmv-audit.md`. The raw script operands are serialized expression
references, not literal movie IDs; unresolved runtime IDs are therefore logged
rather than guessed. Restoration of `SELECT_A`/`SELECT_C` is one proven omitted
frontend event, not a claim that every story trigger has been exhaustively played
on both routes.

### 6.4 Two native Bink presentation backends

The executable can present decoded Bink video in two ways:

- a native OpenGL texture backend, which uploads the decoded image and reaches
  the normal game compositor;
- a presentation-buffer backend using the native Bink buffer lifecycle and a
  direct blit.

The OpenGL backend already enters the renderer's final pipeline. The alternate
backend retains native `BinkDoFrame`, lock, copy, unlock, dirty-rectangle query,
timing and `BinkNextFrame`, but redirects the decoded `BINKSURFACE24R` pixels to
a private proportional canvas and replaces only the final direct blit. Failed
first-path opens are returned unchanged so the game's own alternate-path retry
still occurs. Unsupported formats and rejected scaling fail closed rather than
falling back to distorted output.

Replacement movie packs are outside this repository. They may differ in image,
audio or authored content; the renderer neither supplies nor silently rewrites
them.

## 7. Neutral CRT presentation

The optional CRT mode is a restrained original approximation and is enabled by
default for the low-resolution asset stack. It is a presentation model, not a
color grade. The four stages after MSAA resolve are:

1. conservative encoded-domain debanding, followed by the standard sRGB EOTF into a
   640x480 RGBA16F signal target;
2. beam-aware scanlines and a physical-pixel-stable aperture grille into a
   viewport-sized RGBA16F response target;
3. half-resolution separable highlight blur;
4. neutral bloom/halation, the standard sRGB OETF and sub-LSB output dithering.

Mask gains average to one independently for red, green and blue. Defaults do not
add warmth, saturation, sharpening, curvature, overscan, chromatic aberration,
vignette or composite/RF artifacts. The default framebuffer is cleared to black
and the shader covers only the exact 4:3 viewport, so pillars remain black.

Gameplay and both Bink backends are routed through the same signal reconstruction
when CRT is active. Setting `[CRT] Enabled=0` disables CRT-specific redirection;
proportional scaling, the input guard and lifecycle logging remain active.

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
