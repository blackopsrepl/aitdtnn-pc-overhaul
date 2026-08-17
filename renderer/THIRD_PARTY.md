# Renderer algorithmic references

The renderer is an original integration distributed under `LICENSE.txt`. It
uses two published real-time graphics techniques as small mathematical building
blocks; no third-party shader package, texture or binary is vendored.

## Interleaved Gradient Noise

The dithering hash uses the Interleaved Gradient Noise formula associated with
Jorge Jimenez's *Next Generation Post Processing in Call of Duty: Advanced
Warfare* work:

<https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/>

The renderer high-pass filters that scalar sequence and applies it only at
sub-LSB amplitude.

## Gaussian blur with linear sampling

The bloom pass uses the five-fetch form of a normalized nine-tap Gaussian kernel
described by Daniel Rákos in *Efficient Gaussian blur with linear sampling*:

<https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/>

Only the published numerical weights/offsets and bilinear-sampling method are
used; the surrounding shader, framebuffer pipeline and highlight extraction are
project code.
