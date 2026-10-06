cmake_minimum_required(VERSION 3.25)

# Universal binary, runs on macOS 13+ (incl. macOS 26 Tahoe). Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

project(DumbleSSS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DUMBLE_BUILD_PLUGIN "Build the Standalone/AU targets" ON)
option(DUMBLE_BUILD_TESTS  "Build the DSP unit tests"        ON)
option(DUMBLE_RTSAN        "Build tests with -fsanitize=realtime (LLVM clang >= 20)" OFF)
option(AMP_BUILD_AC30      "Build the VOX AC30C2 plugin and its tests" ON)

#===============================================================================
# JUCE 9.0.3
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

include(cmake/Warnings.cmake)

#===============================================================================
# Shared DSP sources (used by the plugin and by the tests)
set(DUMBLE_DSP_SOURCES
    source/dsp/AmpEngine.cpp)

# AC30C2 DSP sources (plugin + tests). The circuit-modelling core of source/dsp is shared through
# the include path; the AC30 code lives in namespace ac30.
set(AC30_DSP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ac30/dsp/Ac30Engine.cpp)

#===============================================================================
if(DUMBLE_BUILD_PLUGIN)
    if(APPLE)
        set(DUMBLE_FORMATS Standalone AU)
    else()
        set(DUMBLE_FORMATS Standalone) # Linux/Windows: compile check of the shared code only
    endif()

    juce_add_plugin(DumbleSSS
        PRODUCT_NAME                 "Dumble SSS"
        COMPANY_NAME                 "kgoniszewski"
        BUNDLE_ID                    "com.kgoniszewski.dumblesss"
        PLUGIN_MANUFACTURER_CODE     Kgon
        PLUGIN_CODE                  Dsss
        FORMATS                      ${DUMBLE_FORMATS}
        AU_MAIN_TYPE                 kAudioUnitType_Effect
        IS_SYNTH                     FALSE
        NEEDS_MIDI_INPUT             FALSE
        NEEDS_MIDI_OUTPUT            FALSE
        IS_MIDI_EFFECT               FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
        COPY_PLUGIN_AFTER_BUILD      ${APPLE}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT   "Dumble SSS needs audio input access to process your guitar from the audio interface."
        HARDENED_RUNTIME_ENABLED     TRUE
        HARDENED_RUNTIME_OPTIONS     com.apple.security.device.audio-input)

    target_sources(DumbleSSS PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/Parameters.cpp
        source/StandaloneApp.cpp
        ${DUMBLE_DSP_SOURCES})

    target_include_directories(DumbleSSS PRIVATE source)

    target_compile_definitions(DumbleSSS PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

    if(NOT APPLE)
        # headless CI boxes: no ALSA/JACK headers required for a compile check
        target_compile_definitions(DumbleSSS PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
    endif()

    target_link_libraries(DumbleSSS
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    dumble_apply_dsp_flags(DumbleSSS)

    #---------------------------------------------------------------------------
    # VOX AC30C2 (AU + Standalone, no VST3). Reuses source/dsp (Koren tables, CabinetIR, SpringTank, ...)
    # and the AXE I/O One standalone app (source/StandaloneApp.cpp).
    if(AMP_BUILD_AC30)
        juce_add_plugin(VoxAC30C2
            PRODUCT_NAME                 "AC30C2"
            COMPANY_NAME                 "kgoniszewski"
            BUNDLE_ID                    "com.kgoniszewski.ac30c2"
            PLUGIN_MANUFACTURER_CODE     Kgon
            PLUGIN_CODE                  Ac30
            FORMATS                      ${DUMBLE_FORMATS}
            AU_MAIN_TYPE                 kAudioUnitType_Effect
            IS_SYNTH                     FALSE
            NEEDS_MIDI_INPUT             FALSE
            NEEDS_MIDI_OUTPUT            FALSE
            IS_MIDI_EFFECT               FALSE
            EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
            COPY_PLUGIN_AFTER_BUILD      ${APPLE}
            MICROPHONE_PERMISSION_ENABLED TRUE
            MICROPHONE_PERMISSION_TEXT   "AC30C2 needs audio input access to process your guitar from the audio interface."
            HARDENED_RUNTIME_ENABLED     TRUE
            HARDENED_RUNTIME_OPTIONS     com.apple.security.device.audio-input)

        target_sources(VoxAC30C2 PRIVATE
            ac30/PluginProcessor.cpp
            ac30/PluginEditor.cpp
            ac30/Parameters.cpp
            source/StandaloneApp.cpp
            ${AC30_DSP_SOURCES})

        target_include_directories(VoxAC30C2 PRIVATE ac30 source)

        target_compile_definitions(VoxAC30C2 PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

        if(NOT APPLE)
            target_compile_definitions(VoxAC30C2 PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
        endif()

        target_link_libraries(VoxAC30C2
            PRIVATE
                juce::juce_audio_utils
                juce::juce_dsp
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)

        dumble_apply_dsp_flags(VoxAC30C2)
    endif()
endif()

#===============================================================================
if(DUMBLE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
