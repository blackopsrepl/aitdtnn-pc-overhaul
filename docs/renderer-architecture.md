# Renderer, FMV and CRT presentation

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

## 8. Frame pacing

The original PC engine advances gameplay logic from presented frame count, so on
machines faster than the hardware it targeted, gameplay speed and reference
frame-rate assumptions both scale with the display refresh. PCGamingWiki records
the resulting errors above 60 FPS, and the game's FMV player assumes the retail
movies' low authored rate.

The renderer paces the present boundary rather than the game's logic or an OS
timing API. `hooked_swap_buffers` and the alternate Bink backend's
`present_decoded_bink_frame` each call one `pace_present` helper immediately
before the real `SwapBuffers`. Gameplay uses `[FrameRate] GameLimit` (60 FPS by
default). While a movie is playing, the target is the rate read from the live
Bink handle, so retail encodes and community replacement packs are paced to
their own authored rate instead of one hard-coded number. Pacing is applied once
per frame: the game-facing swap hook defers to the alternate backend while that
backend owns the present.

Pacing uses `QueryPerformanceCounter` with a drift-corrected deadline and no
catch-up burst; a high-resolution waitable timer provides the bulk of the wait,
with a short spin for sub-millisecond accuracy. The limiter does not modify the
swap interval: its cap is enforced by the timer rather than by the display
refresh, so the configured maximum holds regardless of the monitor, and VSync
remains available for the swap to synchronize the in-viewport Bink movie
presentation. Diagnostic probes record whether the executable itself sets the
swap interval and whether it calls `BinkWait`, so the exact interaction can be
confirmed on the retail runtime rather than assumed.

This feature is address-agnostic: it depends only on the present and Bink hooks
already required for presentation, so both supported executables receive it
without additional executable signatures. The authored movie rate is read
defensively across the two known Bink 1.x field layouts; an unrecognised handle
falls back to the documented retail rate and logs the observed interval.
