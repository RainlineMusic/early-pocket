# Changelog

## 0.4.0

- Rebuilt the early-reflection renderer as a true-stereo engine.
- Removed the asymmetric `mid + side * 0.35` input collapse.
- Replaced linear fractional delay reads with precomputed cubic interpolation.
- Added deterministic 3/5/7-tap reflection micro-clusters with time-dependent diffusion.
- Added image-source first-order room geometry and selected second-order reflections.
- Added path-specific distance attenuation, surface absorption, HF/air loss, stereo spread and decorrelation.
- Reworked Room Size, Room Shape, Width and Distance behaviour.
- Changed Mix to a smoothed equal-power crossfade.
- Added click-resistant model and EQ crossfades.
- Moved expensive delay/pan/filter preparation out of the per-sample reflection loop.
- Changed the default post-EQ to a neutral response.
- Rebuilt Learn around persistent transient correlation with 0.5 ms profiling and interpolated peak timing.
- Added dry-reference Learn mode: main input = room signal, sidechain = dry source.
- Added source-width-compensated stereo estimation and six-band reflection-colour analysis.
- Learned taps are now retained and rendered directly; macro controls transform the measured model.
- Added backward-compatible state loading and version-2 learned-model serialization.
- Added standalone core DSP regression tests, including multi-rate stability coverage.
- Made Learn capture handoff race-free by moving capture→analysis transition to an audio-block boundary.
- Added a two-platform GitHub Actions workflow producing four separate VST3/AAX artifacts, including verified macOS Universal binaries.
- Fixed macOS VST3 signing order around JUCE manifest generation and added bundle/signature verification in CI.
