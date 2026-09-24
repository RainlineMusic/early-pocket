# Early Pocket 0.4.0

Early Pocket is a dedicated early-reflections processor built with JUCE. Version 0.4.0 replaces the original sparse multi-tap model with a true-stereo, geometry-informed early-reflection engine and upgrades Learn so measured reflections can become the actual base model instead of being discarded after fitting.

## DSP changes in 0.4.0

- True stereo-in/stereo-out reflection routing; the old left-biased mono collapse is removed.
- Cubic fractional-delay interpolation instead of linear interpolation.
- Each visible reflection is rendered as a small deterministic micro-cluster. Later paths become progressively more diffuse without random pitch movement.
- Six first-order paths are generated from a rectangular image-source model (left/right/front/rear/floor/ceiling). Faces above six add selected second-order paths.
- Per-path distance loss, wall reflectivity, air/HF absorption, stereo spread and decorrelation.
- Room Size, Room Shape, Width and Distance now transform timing, geometry, buildup and spectral behaviour rather than only scaling fixed taps.
- Smooth model crossfades, EQ coefficient crossfades and equal-power smoothed Mix.
- Neutral default post-EQ (180 Hz / 900 Hz / 4.5 kHz, 0 dB).
- Expensive delay, pan and cluster coefficients are precomputed when the model changes rather than in the sample loop.

## Learn 2

Without a sidechain, Learn performs autonomous analysis of the main/room signal. With the sidechain enabled, the main input is treated as the room/processed recording and the sidechain as the dry reference. The host should present those two paths time-aligned (normal plug-in delay compensation is sufficient); Learn intentionally does not guess arbitrary recording offsets because periodic material can make automatic lag estimation ambiguous. Learn uses transient persistence and whitened normalized correlation at 0.5 ms profile resolution, sub-bin peak interpolation, source-width-compensated stereo analysis and a six-band direct-vs-reflection spectral estimate.

The detected delays, levels, pan, spectral damping and diffusion are retained as a learned base model. After learning, Room Size / Shape / Width / Distance transform that measured model. Increasing Faces beyond the measured tap count supplements it with geometry-generated paths.

## Local build

JUCE 8 is required. VST3 only on macOS:

```sh
cmake -S . -B build-vst3 -DJUCE_DIR=/path/to/JUCE-8 \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-vst3 --config Release --target EarlyPocket_VST3
```

VST3 + AAX Native:

```sh
cmake -S . -B build-aax -DJUCE_DIR=/path/to/JUCE-8 \
  -DAAX_SDK_PATH=/path/to/aax-sdk \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-aax --config Release --target EarlyPocket_VST3 EarlyPocket_AAX
```

The AAX SDK is not included. It must be obtained under Avid's SDK terms. The CMake build performs ad-hoc macOS code signing so the generated bundle is structurally valid for testing/build verification, but it does **not** perform Avid/PACE commercial distribution signing. AAX deployment in Pro Tools may therefore require the normal Avid/PACE signing step appropriate to your distribution setup.

## GitHub Actions

`.github/workflows/build-plugins.yml` builds and tests both platforms. A successful run publishes exactly four artifacts:

1. `Early-Pocket-Windows-VST3`
2. `Early-Pocket-Windows-AAX`
3. `Early-Pocket-macOS-Universal-VST3`
4. `Early-Pocket-macOS-Universal-AAX`

Before running the workflow, add the repository secret `AAX_SDK_URL` containing an authorized downloadable ZIP of the AAX SDK. If that URL requires bearer authentication, also set `AAX_SDK_TOKEN`. The SDK is downloaded only on the runner and is not committed or uploaded as an artifact.

The macOS job builds both `arm64` and `x86_64` into each plug-in bundle, verifies both architectures with `lipo`, checks the VST3 manifest, and verifies the final ad-hoc bundle signatures before upload.

## Core tests

The repository contains standalone tests for model validity, true-stereo symmetry and dry-reference Learn recovery:

```sh
cmake --build build --config Release --target EarlyCoreTests
ctest --test-dir build -C Release --output-on-failure
```
