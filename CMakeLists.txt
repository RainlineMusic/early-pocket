cmake_minimum_required(VERSION 3.22)
project(EarlyPocket VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED JUCE_DIR OR NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set JUCE_DIR to a local JUCE source checkout, for example -DJUCE_DIR=/path/to/JUCE")
endif()

set(EARLY_FORMATS VST3)
if(DEFINED AAX_SDK_PATH)
    if(NOT EXISTS "${AAX_SDK_PATH}/Interfaces/AAX.h")
        message(FATAL_ERROR "AAX_SDK_PATH must point to the root of the AAX SDK")
    endif()
    set(JUCE_GLOBAL_AAX_SDK_PATH "${AAX_SDK_PATH}")
    list(APPEND EARLY_FORMATS AAX)
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

add_library(EarlyCore STATIC
    Source/Core/EarlyModel.cpp
    Source/Core/EarlyModel.h
    Source/Core/EarlyEngine.cpp
    Source/Core/EarlyEngine.h
    Source/Core/LearnAnalyzer.cpp
    Source/Core/LearnAnalyzer.h)
target_include_directories(EarlyCore PUBLIC Source)
if(MSVC)
    target_compile_options(EarlyCore PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(EarlyCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

juce_add_plugin(EarlyPocket
    COMPANY_NAME "Early Pocket"
    BUNDLE_ID "com.earlypocket.audio"
    AAX_IDENTIFIER "com.earlypocket.audio"
    PLUGIN_MANUFACTURER_CODE Epkt
    PLUGIN_CODE ErPk
    FORMATS ${EARLY_FORMATS}
    PRODUCT_NAME "Early Pocket"
    DESCRIPTION "Early reflections processor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE
    VST3_CATEGORIES Fx Reverb
    AAX_CATEGORY Reverb)

juce_generate_juce_header(EarlyPocket)
target_sources(EarlyPocket PRIVATE
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginProcessor.h
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/PluginEditor.h)
target_link_libraries(EarlyPocket PRIVATE
    EarlyCore
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(EarlyPocket PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(TARGET EarlyPocket_VST3)
    juce_enable_vst3_manifest_step(EarlyPocket)
    if(APPLE)
        add_custom_command(TARGET EarlyPocket_VST3 POST_BUILD
            COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:EarlyPocket_VST3>"
            VERBATIM)
    endif()
endif()

if(TARGET EarlyPocket_AAX AND APPLE)
    add_custom_command(TARGET EarlyPocket_AAX POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:EarlyPocket_AAX>"
        VERBATIM)
endif()
