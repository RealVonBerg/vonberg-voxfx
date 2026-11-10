cmake_minimum_required(VERSION 3.22)

project(VonBergVoxFX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local copy if JUCE_DIR is given, otherwise download it from GitHub.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

if(NOT DEFINED VOX_FORMATS)
    set(VOX_FORMATS VST3 Standalone)
endif()

set(VOX_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(VOX_SOURCES
    Source/VoxEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

if(NOT VOX_TESTS_ONLY)
    juce_add_plugin(VonBergVoxFX
        COMPANY_NAME "VonBerg"
        COMPANY_WEBSITE ""
        PLUGIN_MANUFACTURER_CODE Vnbg
        PLUGIN_CODE VbVx
        PRODUCT_NAME "VonBerg Vox FX"
        BUNDLE_ID com.vonberg.voxfx
        FORMATS ${VOX_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx)

    target_sources(VonBergVoxFX PRIVATE ${VOX_SOURCES})
    target_compile_definitions(VonBergVoxFX PUBLIC ${VOX_COMMON_DEFS})
    if(MSVC)
        target_compile_options(VonBergVoxFX PRIVATE /utf-8)
    endif()
    target_link_libraries(VonBergVoxFX
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# Offline tests (not part of the plugin)
if(VOX_BUILD_TESTS)
    set(LINUX_HEADLESS_DEFS JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0 JUCE_ALSA=0 JUCE_JACK=0)

    juce_add_console_app(EngineTest PRODUCT_NAME "EngineTest")
    target_sources(EngineTest PRIVATE tests/EngineTest.cpp Source/VoxEngine.cpp)
    target_compile_definitions(EngineTest PRIVATE ${VOX_COMMON_DEFS} ${LINUX_HEADLESS_DEFS})
    target_link_libraries(EngineTest PRIVATE juce::juce_audio_formats juce::juce_recommended_config_flags)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/PluginTest.cpp)
        juce_add_console_app(PluginTest PRODUCT_NAME "PluginTest")
        target_sources(PluginTest PRIVATE tests/PluginTest.cpp ${VOX_SOURCES})
        target_compile_definitions(PluginTest PRIVATE ${VOX_COMMON_DEFS} ${LINUX_HEADLESS_DEFS}
            JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="VonBerg Vox FX" JUCE_STANDALONE_APPLICATION=1)
        target_link_libraries(PluginTest PRIVATE
            juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
            juce::juce_recommended_config_flags)
    endif()
endif()
