# Early Pocket 0.4.3

Early Pocket is a dedicated early-reflections processor built with JUCE. It uses a true-stereo, geometry-informed early-reflection engine with a quieter secondary layer.

Version 0.4.3 gives Width a much larger audible range: 0% folds the reflections to mono, 100% yields a broad stereo response, and 200% emphasizes laterally arriving energy. The left and right wall responses retain equal nominal level, while slightly different path lengths prevent their stereo contributions from cancelling. Slow L/R correction follows the source's long-term channel balance. The earliest prominent arrival stays crisp; subsequent arrivals keep the phase dispersion used to reduce combing. A small gain adjustment limits the added side energy at high Width.

Version 0.4.2 lets large rooms produce prominent paths beyond 200 ms while keeping small rooms compact. Distance runs from close (0%) to distant (100%), and Room Shape keeps a centred source and listener with matched left/right wall response. Learn searches the available parameters and then runs the same procedural model as manual control. The EQ has a fixed mid-band Q of 0.7; its bypass button and Q control have been removed. Double-click or Alt-click a main dial or EQ node to reset it; Shift-drag an EQ node for fine adjustment. Older version-2 procedural presets migrate their Distance value when loaded.

The spatial design follows the measured role of early lateral reflections in apparent source width (Barron and Marshall, *Journal of Sound and Vibration* 77, 1981, DOI: 10.1016/S0022-460X(81)80020-X), and the perceptual dominance of the first arriving wavefront (Brown, Stecker and Tollin, *Journal of the Association for Research in Otolaryngology* 16, 2015, DOI: 10.1007/s10162-014-0496-2). Bradley, Reich and Norcross (*JASA* 108, 2000, DOI: 10.1121/1.429597) distinguish the broadening associated with early lateral arrivals from later envelopment. These results motivate the lateral arrival pattern and the crisp first reflection; they do not specify the proprietary VSS3 processing.

Faces sets the number of prominent reflection paths. The processor also creates quieter secondary paths to soften coloration; the graph displays these as a diffuse area behind the individual primary-path lines.

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

## Learn

Without a sidechain, Learn performs autonomous analysis of the main/room signal. With the sidechain enabled, the main input is treated as the room/processed recording and the sidechain as the dry reference. The host should present those two paths time-aligned (normal plug-in delay compensation is sufficient); Learn intentionally does not guess arbitrary recording offsets because periodic material can make automatic lag estimation ambiguous. Learn uses transient persistence and whitened normalized correlation at 0.5 ms profile resolution, sub-bin peak interpolation, source-width-compensated stereo analysis and a six-band direct-vs-reflection spectral estimate.

Learn detects reflection arrivals and estimates stereo width and colour, then searches Room Size, Room Shape, Distance and Faces for the closest procedural timing pattern. It sets Width and the three EQ gains from the stereo and spectral measurements. The target is retained only for the grey reference overlay in the graph. A sparse or ambiguous target can be rejected; matching a proprietary reverb pattern exactly is not implied. Older presets with learned taps now render from their saved knob positions instead of an invisible learned response, so their sound may change.

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
