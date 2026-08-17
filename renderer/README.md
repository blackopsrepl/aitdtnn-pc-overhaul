# AITD:TNN PC renderer

This module modernizes the verified English 15-slot/no-CD PC renderer without
changing `alone4.exe` on disk and without replacing Bink, audio, input or game
assets.

Supported executable SHA-256:

`5668118e0e19d569986500a1c805a85397c8681e7b672b49a68645462eccc672`

Public initializer:

```cpp
extern "C" DWORD WINAPI AITD4_Initialize(void* reserved);
```

Implemented production path:

- DPI-aware borderless fullscreen on the primary monitor.
- Exact centered 4:3 output. A 1920x1080 monitor uses a 1440x1080 game
  viewport and 240-pixel black pillars; projection, UI, masks and FMVs keep
  their original proportions.
- OpenGL 3.3 compatibility context so the fixed-function game remains native.
- RGBA8 off-screen rendering with 24-bit depth, 8-bit stencil and configurable
  4x MSAA resolve.
- VSync enabled by default.
- Up to 16x anisotropic filtering for mipmapped textures while non-mipmapped
  backgrounds, masks, UI and video retain their original filtering.
- Legacy `GL_CLAMP` conversion to `GL_CLAMP_TO_EDGE` to avoid border-color edge
  sampling on modern drivers.
- Restrained gradient debanding followed by sub-LSB dithering. No gamma,
  saturation, grading or sharpening transform is applied.
- Optional neutral 640x480 CRT approximation, enabled by default: standard
  linear-light conversion, beam-aware scanlines, a restrained aperture grille,
  and mild neutral bloom/halation. Curvature, overscan, chromatic aberration,
  vignette and composite artifacts remain disabled.
- Complete OpenGL state isolation around the final compositor, including
  hostile incoming masks, polygon mode, logic operations and framebuffer
  state.
- Proportional native-Bink canvas placement inside the same 4:3 viewport.
  Both executable backends are covered. The active native OpenGL movie backend
  decodes and draws into the same offscreen framebuffer as gameplay, then uses
  the normal CRT compositor. The alternate presentation-buffer backend retains
  native decode, lock/unlock, dirty-rectangle query, timing and advancement,
  redirects decoded `BINKSURFACE24R` pixels to a private 640x480 canvas, and
  replaces its direct Bink blit with one CRT presentation. With CRT disabled,
  CRT-specific redirection is disabled while proportional scaling, the input
  guard and lifecycle logging remain active.
- A neutral-release movie skip gate, native character-selection restoration
  for `SELECT_A` and `SELECT_C` at the verified post-portrait/title continuation,
  and an FMV lifecycle ledger. Native Bink remains responsible for decoding,
  timing, audio and frame advancement. The renderer owns final presentation
  while CRT mode is enabled.
- Development-only shader hot reload and capture controls, disabled by default.

Deliberately excluded: widescreen projection, texture replacement, AI texture
upscaling, fog redesign, stylized color grading and undocumented artistic model
or shadow changes. The renderer does not distribute copyrighted assets.

The complete physical movie inventory, statically identified controller
callsites, unresolved script-node inventory and evidence boundaries are
documented in [docs/fmv-audit.md](docs/fmv-audit.md).
The original presentation design is documented in
[docs/crt-design.md](docs/crt-design.md).
