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

- DPI-aware borderless fullscreen on the selected monitor.
- Exact centered 4:3 output. A 1920x1080 monitor uses a 1440x1080 game
  viewport and 240-pixel black pillars; projection, UI, masks and FMVs keep
  their original proportions.
- OpenGL 3.3 compatibility context so the fixed-function game remains native.
- RGBA8 off-screen rendering with 24-bit depth, 8-bit stencil and configurable
  4x MSAA resolve.
- VSync enabled by default.
- Up to 16x anisotropic filtering for mipmapped 3D textures while non-mipmapped
  backgrounds, masks, UI and video retain their original filtering.
- Legacy `GL_CLAMP` correction to `GL_CLAMP_TO_EDGE` to prevent verified edge
  sampling seams.
- Restrained gradient debanding followed by sub-LSB dithering. No gamma,
  saturation, grading or sharpening transform is applied.
- Complete OpenGL state isolation around the final compositor, including
  hostile incoming masks, polygon mode, logic operations and framebuffer
  state.
- Proportional native-Bink canvas scaling and centering inside the same 4:3
  viewport, while retaining the game's normal alternate-path retry.
- A neutral-release movie skip gate, native character-selection restoration
  for `SELECT_A` and `SELECT_C` at the verified post-portrait/title continuation,
  and an FMV lifecycle ledger. Native Bink remains responsible for decoding,
  timing, audio and presentation.
- Development-only shader hot reload and capture controls, disabled by default.

Deliberately excluded: widescreen projection, texture replacement, AI texture
upscaling, fog redesign, stylized color grading and undocumented artistic model
or shadow changes. The renderer does not distribute copyrighted assets.

The complete physical movie inventory, trigger paths and evidence boundaries
are documented in [docs/fmv-audit.md](docs/fmv-audit.md).
