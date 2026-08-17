# Neutral CRT presentation

The optional CRT mode is a restrained original approximation. A local
CRT-configured game was inspected only to establish the enabled/disabled visual
feature set; no third-party shader package was copied. The two small published
mathematical techniques used for noise and blur are credited in
`../THIRD_PARTY.md`; the CRT response and integration are project code.

The production path is bounded to four stages after the game's four-sample MSAA
resolve:

1. conservative encoded-domain debanding, then the standard sRGB EOTF into a 640x480
   RGBA16F signal target;
2. beam-aware scanlines and a physical-pixel-stable aperture grille into a
   viewport-sized RGBA16F response target;
3. half-resolution separable highlight blur;
4. neutral bloom/halation, the standard sRGB OETF and sub-LSB final dithering.

Mask gains average to one independently for red, green and blue over each
three-pixel triad. No grading, saturation matrix, sharpening, curvature,
overscan, chromatic aberration, vignette or composite/RF simulation is applied.
The default framebuffer is cleared to black before the final pass and the CRT
shader only covers the exact 4:3 viewport, leaving pillars truly black.

The supported executable contains two native Bink presentation backends. The
installed OpenGL configuration selects a direct texture backend: Bink decodes
to the game's native RGB surface, the game uploads and draws that surface into
the renderer's offscreen framebuffer, and its normal SwapBuffers call performs
the CRT pass. No `BinkBufferBlit` exists on that path, so none is fabricated or
suppressed. The renderer tracks every decoded frame through the final swap.

The alternate presentation-buffer backend is also supported. There the
renderer owns the verified high-level movie-frame callsite while preserving
`BinkDoFrame`, native buffer lock, copy, native unlock, dirty-rectangle query
and conditional `BinkNextFrame`. Only the copy destination and presentation
change: the observed RGB `BINKSURFACE24R` frame is decoded into a private buffer
using the native pitch and orientation, uploaded as a black-letterboxed 640x480
RGBA canvas, and presented through the CRT pipeline exactly where the original
routine would call `BinkBufferBlit`. That direct window blit is suppressed.
Unsupported formats fail closed. With CRT disabled, CRT-specific redirection is
disabled; proportional scaling, the input guard and lifecycle logging remain
active.
