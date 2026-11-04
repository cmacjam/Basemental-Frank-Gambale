cmake_minimum_required(VERSION 3.22)
project(Fusion250 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- plug-in formats per platform -------------------------------------------
set(FUSION_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND FUSION_FORMATS AU)
elseif(UNIX)
    list(APPEND FUSION_FORMATS LV2)
endif()

# Pro Tools: pass -DAAX_SDK_PATH=/path/to/aax-sdk (free from Avid's developer site)
set(AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (optional)")
if(AAX_SDK_PATH)
    juce_set_aax_sdk_path(${AAX_SDK_PATH})
    list(APPEND FUSION_FORMATS AAX)
endif()

juce_add_plugin(Fusion250
    PRODUCT_NAME            "Fusion 250"
    COMPANY_NAME            "Fusion Audio"
    BUNDLE_ID               com.fusionaudio.fusion250
    PLUGIN_MANUFACTURER_CODE Fsna
    PLUGIN_CODE             F250
    FORMATS                 ${FUSION_FORMATS}
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES         Fx Distortion
    AU_MAIN_TYPE            kAudioUnitType_Effect
    AAX_CATEGORY            AAX_ePlugInCategory_Harmonic
    LV2URI                  "https://fusionaudio.example/plugins/fusion250")

target_sources(Fusion250 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TunerPanel.cpp)

target_include_directories(Fusion250 PRIVATE Source)

target_compile_definitions(Fusion250 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Fusion250
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- offline DSP test (no JUCE) ----------------------------------------------
add_executable(render_test tests/render_test.cpp)
target_include_directories(render_test PRIVATE Source)
add_executable(tuner_test tests/tuner_test.cpp)
target_include_directories(tuner_test PRIVATE Source)

# ---- optional end-to-end GUI/tuner test (links the real plug-in code) ---------
option(FUSION_GUI_TEST "Build the gui_snapshot end-to-end test" OFF)
if(FUSION_GUI_TEST)
    # compiles against the plug-in's shared-code library with the same settings
    add_executable(gui_snapshot tests/gui_snapshot.cpp)
    target_include_directories(gui_snapshot PRIVATE Source $<TARGET_PROPERTY:Fusion250,INCLUDE_DIRECTORIES>)
    target_compile_definitions(gui_snapshot PRIVATE $<TARGET_PROPERTY:Fusion250,COMPILE_DEFINITIONS>)
    target_link_libraries(gui_snapshot PRIVATE Fusion250 pthread dl X11 Xext freetype fontconfig)
endif()
