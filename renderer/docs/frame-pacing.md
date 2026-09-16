# Frame pacing design

This note records why the overhaul paces the present boundary, how the cinematic
rate is selected, and what still needs runtime confirmation.

## Why pace the present

The PC engine advances its gameplay logic once per presented frame. On hardware
faster than the 2001 target, both the simulation speed and any frame-count-based
assumption scale with the display refresh. PCGamingWiki documents errors at
120+ FPS and recommends capping the game to 60 FPS or using 60 Hz with VSync.
The game's FMV player additionally assumes the low rate of the retail movie
encodes, and can crash when it advances the Bink frames faster than intended.

Capping inside game code would require new address hooks and a new executable
signature. The renderer already mediates every present, so it can delay the
frame there instead. The delay propagates back through the game loop, lowering
both the displayed and simulated rate without touching the executable. The only
two present paths are `hooked_swap_buffers` and, for the alternate Bink backend,
`present_decoded_bink_frame`; each calls `pace_present` immediately before the
real `SwapBuffers`.

## Selecting the cinematic rate

`MovieLimit=Auto` reads the frame rate from the live Bink handle. This matters
because the retail movies and community replacement packs are authored at
different rates: pacing everything to a literal 15 FPS would play a 30 FPS
replacement pack at half speed and risk audio desynchronisation. Authoring the
pace to the file keeps both correct.

Bink 1.x exposes a frame rate through its handle. Two field layouts are checked
in order and a candidate is accepted only when the implied rate is plausible
(whole-number frames per second between 5 and 120, divisor not larger than the
rate). When neither layout is plausible the fallback is the retail rate. The
chosen interval is logged per movie so the accepted layout can be confirmed on
the real runtime.

A numeric `MovieLimit` overrides the authored rate for any special case.

## Timing and VSync

Pacing uses `QueryPerformanceCounter` and a deadline that advances by one
interval per frame. A frame that arrives late resynchronises from the current
time instead of bursting to catch up. The bulk of each wait uses a
high-resolution waitable timer (falling back to `Sleep`), followed by a short
spin of up to roughly 400 microseconds for sub-millisecond accuracy. No global
timer-resolution change is requested, and no WinMM entry point is imported, so
the module does not depend on the app-local `winmm.dll` proxy.

The limiter does not modify the swap interval. The cap is enforced by the timer,
not by the display refresh, because the refresh is decided by the driver and
monitor and cannot be relied on to produce 60 Hz. VSync is therefore left as
configured; the swap remains available to synchronize the in-viewport Bink movie
presentation.

## Verifying the VSync interaction

The Bink Windows buffer API is DirectDraw-based and its presentation path was
vsynced in the original engine, so keeping VSync enabled is the conservative
choice. Whether VSync is strictly required for correct in-viewport movie
playback has not been proven statically, so the renderer installs two
non-invasive probes:

- `_BinkWait@4` is detoured when the executable imports it, logging the call
  and return pattern. This shows whether the engine self-paces movie frames
  through Bink's own clock or depends on the present rate.
- `wglSwapIntervalEXT` is detoured if the executable looks it up through
  `GetProcAddress`, logging any interval the game itself requests.

A retail run that plays an FMV and reports these log lines settles the
question. Until then, no behavior depends on the answer, because the limiter
never touches the swap interval.

## Configuration

```ini
[FrameRate]
Enabled=1
GameLimit=60
MovieLimit=Auto
```

- `Enabled=0` disables pacing and restores the configured VSync behaviour.
- `GameLimit`/`MovieLimit` accept a frame rate, `0` for uncapped, or `Auto`.
- Changes apply live; the limiter re-reads configuration every present.

## Open verification

The authored-rate field layout is read defensively and logged, but it has not
been confirmed against the retail and replacement Bink files in a published
runtime run. The acceptance step is to play a retail movie and a replacement
movie and confirm the logged `authored_interval_us` matches each file's authored
rate and that each completes without the frame-advance crash.
