cmake_minimum_required(VERSION 3.22)
project(Fusion250 VERSION 1.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- Neural Amp Modeler core (runs NAM captures, A1 + A2) ---------------------
# Pinned to the commit TONE3000's API examples use. Eigen comes from its GitHub mirror at
# the commit NAM pins (NAM's own submodule points at gitlab).
set(NAM_CORE_COMMIT 0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842)
set(NAM_EIGEN_COMMIT bc3b39870ecb690a623a3f49149a358b95c5781d)
FetchContent_Declare(namcore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        ${NAM_CORE_COMMIT}
    GIT_SUBMODULES "")
FetchContent_Declare(nameigen
    GIT_REPOSITORY https://github.com/eigen-mirror/eigen.git
    GIT_TAG        ${NAM_EIGEN_COMMIT}
    GIT_SUBMODULES "")
FetchContent_GetProperties(namcore)
if(NOT namcore_POPULATED)
    FetchContent_Populate(namcore)
endif()
FetchContent_GetProperties(nameigen)
if(NOT nameigen_POPULATED)
    FetchContent_Populate(nameigen)
endif()
file(GLOB NAM_SOURCES "${namcore_SOURCE_DIR}/NAM/*.cpp" "${namcore_SOURCE_DIR}/NAM/*/*.cpp")
# OBJECT library: NAM registers its architectures with static initialisers, which a
# static archive would drop
add_library(namcore OBJECT ${NAM_SOURCES})
set_target_properties(namcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(namcore PUBLIC "${namcore_SOURCE_DIR}")
target_include_directories(namcore SYSTEM PUBLIC "${nameigen_SOURCE_DIR}" "${namcore_SOURCE_DIR}/Dependencies/nlohmann")
target_compile_definitions(namcore PUBLIC NAM_ENABLE_A2_FAST EIGEN_NO_DEBUG EIGEN_DONT_PARALLELIZE)
if(MSVC)
    target_compile_options(namcore PRIVATE /O2 /bigobj /w)
else()
    target_compile_options(namcore PRIVATE -O3 -w)
endif()
if(APPLE)
    set_target_properties(namcore PROPERTIES OSX_ARCHITECTURES "${CMAKE_OSX_ARCHITECTURES}")
endif()

# ---- TONE3000 (your publishable key: -DT3K_PUBLISHABLE_KEY=t3k_pub_... or the env var) --
# Basemental's publishable key (account cmacjam). Publishable keys are made to ship inside
# apps (it's the OAuth client_id); the t3k_cs_ secret key must never be in this repo.
set(T3K_DEFAULT_KEY "t3k_pub_2Q5KygGCnsbheO1gPqzlkzzAeE8uQkEC")
if(NOT DEFINED T3K_PUBLISHABLE_KEY)
    if(NOT "$ENV{T3K_PUBLISHABLE_KEY}" STREQUAL "")
        set(T3K_PUBLISHABLE_KEY "$ENV{T3K_PUBLISHABLE_KEY}")
    else()
        set(T3K_PUBLISHABLE_KEY "${T3K_DEFAULT_KEY}")
    endif()
endif()
set(T3K_PUBLISHABLE_KEY "${T3K_PUBLISHABLE_KEY}" CACHE STRING "TONE3000 publishable key (t3k_pub_...); empty = browse disabled")
set(T3K_API_DOMAIN "https://www.tone3000.com" CACHE STRING "TONE3000 API origin")
set(T3K_PUBLISHABLE_KEY_VALUE "${T3K_PUBLISHABLE_KEY}")
set(T3K_API_DOMAIN_VALUE "${T3K_API_DOMAIN}")
configure_file(Source/T3K/T3KConfig.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/T3KConfig.h" @ONLY)
if(T3K_PUBLISHABLE_KEY STREQUAL "")
    message(STATUS "TONE3000: no publishable key, the Browse buttons will be disabled (files still load)")
else()
    message(STATUS "TONE3000: publishable key set")
endif()

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

# Linux needs libcurl for HTTPS (TONE3000); macOS/Windows use the OS networking
option(FUSION_CURL "Link libcurl on Linux (TONE3000 downloads)" ON)
if(UNIX AND NOT APPLE AND FUSION_CURL)
    set(FUSION_NEEDS_CURL TRUE)
else()
    set(FUSION_NEEDS_CURL FALSE)
endif()

juce_add_plugin(Fusion250
    PRODUCT_NAME            "Basemental FG 250"
    COMPANY_NAME            "Basemental"
    BUNDLE_ID               com.basemental.fg250
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
    LV2URI                  "https://basemental.example/plugins/fg250"
    NEEDS_CURL              ${FUSION_NEEDS_CURL})

target_sources(Fusion250 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TunerPanel.cpp
    Source/ExternalTone.cpp
    Source/T3KPanel.cpp
    Source/T3K/AuthFlow.cpp
    Source/T3K/Client.cpp
    Source/T3K/Http.cpp
    Source/T3K/LoopbackServer.cpp
    Source/T3K/OAuth.cpp
    Source/T3K/SecureStore.cpp
    Source/T3K/Types.cpp
    $<TARGET_OBJECTS:namcore>)

target_include_directories(Fusion250 PRIVATE Source Source/T3K "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(Fusion250 PRIVATE "${namcore_SOURCE_DIR}")
target_include_directories(Fusion250 SYSTEM PRIVATE "${nameigen_SOURCE_DIR}" "${namcore_SOURCE_DIR}/Dependencies/nlohmann")
target_compile_definitions(Fusion250 PRIVATE NAM_ENABLE_A2_FAST EIGEN_NO_DEBUG EIGEN_DONT_PARALLELIZE T3K_EMBEDDED_WEBVIEW=0)
if(APPLE)
    target_link_libraries(Fusion250 PRIVATE "-framework Security")
elseif(WIN32)
    target_link_libraries(Fusion250 PRIVATE crypt32)
endif()

target_compile_definitions(Fusion250 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Fusion250
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- offline DSP test (no JUCE) ----------------------------------------------
add_executable(render_test tests/render_test.cpp)
target_include_directories(render_test PRIVATE Source)
add_executable(tuner_test tests/tuner_test.cpp)
add_executable(comp_test tests/comp_test.cpp)
target_include_directories(comp_test PRIVATE Source)
add_executable(tuner_stress_test tests/tuner_stress_test.cpp)
target_include_directories(tuner_stress_test PRIVATE Source)
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
