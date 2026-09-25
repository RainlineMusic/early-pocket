# Changelog

## 0.4.3

- Reworked Width so 100% creates a wider reflection field and 200% brings strong lateral energy without an intentional fixed L/R gain offset.
- Gave opposite walls different but paired arrival times, and alternated the leading side of secondary reflection pairs.
- Added slow source-relative L/R energy matching for programme material and modest output gain compensation at high Width.
- Kept the first prominent wavefront undiffused and un-dispersed to preserve attack definition; later reflections retain decorrelation.
- Added mono balance, Width progression and first-arrival regression coverage.

## 0.4.2

- Expanded large-room geometry; at Room Size 100% the six main paths extend past 200 ms, with the secondary layer reaching toward 300 ms.
- Reworked Learn as a bounded search over Room Size, Room Shape, Faces and Distance. The resulting knobs alone determine the audible model; Width and EQ are estimated from measured stereo and tone.
- Allowed a single dry-reference transient to yield a fair Learn fit when multiple reflection arrivals are detected.
- Removed the EQ bypass switch and user-adjustable Q; the mid EQ uses fixed Q 0.7.
- Added regression coverage for large-room arrival times and procedural parameter fitting.

## 0.4.1

- Show the secondary reflection density as a diffuse layer in the graph while drawing only Faces-selected primary paths as lines.
- Reduced specular reflection level in small rooms, added a quiet paired diffusion layer and path-specific all-pass dispersion.
- Corrected Distance direction and removed Room Shape's left/right energy bias.
- Added an EQ bypass, adjustable mid-band Q, precise Shift-drag and default reset by Alt-click or double-click.
- Ensured Learn analysis runs once per capture and applies its fitted state together.
- Added stereo balance, Distance and EQ bypass regression coverage.
- Let GitHub Actions use an authorized AAX SDK ZIP when supplied through repository secrets.

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
