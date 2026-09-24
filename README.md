# Early Pocket 0.2.0

JUCE audio plug-in with VST3 and AAX Native targets. No standalone application target is defined. The AAX DSP patch from 2019 is not used: it targets JUCE 5.4.1 and Avid TI DSP hardware, while this project targets native AAX with JUCE 8.

## Local macOS build

JUCE 8 is required. VST3-only build:

```sh
cmake -S . -B build-vst3 -DJUCE_DIR=/path/to/JUCE-8 \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-vst3 --config Release --target EarlyPocket_VST3
```

VST3 and AAX Native build (Avid AAX SDK 2.9 or newer required):

```sh
cmake -S . -B build-aax -DJUCE_DIR=/path/to/JUCE-8 \
  -DAAX_SDK_PATH=/path/to/aax-sdk \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-aax --config Release --target EarlyPocket_VST3 EarlyPocket_AAX
```

The SDK is not included in this source package. AAX SDK terms and Pro Tools signing requirements are set by Avid; this build is not PACE signed.

## Windows GitHub Actions build

Upload this source tree to GitHub and run **Actions → Build Windows plug-ins**. The workflow builds the Windows x64 VST3 artifact. To also build AAX Native, configure the repository secrets `AAX_SDK_URL` and (if the URL needs authentication) `AAX_SDK_TOKEN` with access to an authorized AAX SDK 2.9 ZIP, then run the workflow with `build_aax` enabled. The SDK is downloaded to the runner and is never stored in this repository or its build artifacts.
