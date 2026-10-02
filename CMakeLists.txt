cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
# Static MSVC runtime so Windows users don't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Delibab VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DELIBAB_BUILD_CLAP  "Build the CLAP format (via clap-juce-extensions)" ON)
option(DELIBAB_BUILD_TESTS "Build the headless test/render tool"             ON)

include(FetchContent)

# Pinned JUCE version. To use a local checkout instead of downloading:
#   cmake -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE ...
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(DELIBAB_FORMATS VST3 AU Standalone)
else()
    set(DELIBAB_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Delibab
    PRODUCT_NAME              "Delibab"
    COMPANY_NAME              "vikkrc"
    BUNDLE_ID                 com.vikkrc.delibab
    DESCRIPTION               "Granular synthesizer"
    PLUGIN_MANUFACTURER_CODE  Vkrc
    PLUGIN_CODE               Dlbb
    FORMATS                   ${DELIBAB_FORMATS}
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Instrument Synth Sampler
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice)

# Sources shared by the plugin and the headless test tool.
set(DELIBAB_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Parameters.cpp
    src/dsp/SampleData.cpp
    src/dsp/GrainEngine.cpp
    src/dsp/Effects.cpp
    src/ui/Theme.cpp
    src/ui/WaveformView.cpp
    src/ui/LayerPanel.cpp
    src/ui/GlobalPanel.cpp)

set(DELIBAB_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    DELIBAB_VERSION_STRING="${PROJECT_VERSION}")

target_sources(Delibab PRIVATE ${DELIBAB_SOURCES})
target_include_directories(Delibab PRIVATE src)
target_compile_definitions(Delibab PUBLIC ${DELIBAB_DEFINITIONS})
target_link_libraries(Delibab
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(DELIBAB_BUILD_CLAP)
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        55525c9858d4b25687be7759a5e0f70eccef218e # main after "JUCE 9 support" (#178); no release tag yet
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(clap-juce-extensions)
    clap_juce_extensions_plugin(TARGET Delibab
        CLAP_ID       "com.vikkrc.delibab"
        CLAP_FEATURES instrument synthesizer sampler stereo)
endif()

if(DELIBAB_BUILD_TESTS)
    juce_add_console_app(DelibabTests PRODUCT_NAME "DelibabTests")
    target_sources(DelibabTests PRIVATE ${DELIBAB_SOURCES} tests/TestMain.cpp)
    target_include_directories(DelibabTests PRIVATE src)
    target_compile_definitions(DelibabTests PRIVATE ${DELIBAB_DEFINITIONS} DELIBAB_HEADLESS_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(DelibabTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
