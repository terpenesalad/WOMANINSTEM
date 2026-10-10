cmake_minimum_required(VERSION 3.24)

# ------------------------------------------------------------------------------
#  WOMANINSTEM - stem separation + play-along amp rig
# ------------------------------------------------------------------------------
project(WOMANINSTEM VERSION 3.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WIS_BUILD_APP   "Build the GUI application"                      ON)
option(WIS_BUILD_TOOLS "Build command line tools (stem CLI, DSP tests)"   ON)
option(WIS_AVX2        "Compile with AVX2 (much faster separation)"       ON)
set(WIS_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (folder containing 'common'). Enables ASIO on Windows.")

if(MSVC)
    # Static runtime -> single self-contained exe, no VC++ redistributable needed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---- Dependencies ------------------------------------------------------------
# JUCE: application framework, audio I/O (ASIO/WASAPI), audio file decoding, DSP.
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON
    GIT_SUBMODULES "")

# Eigen: linear algebra used by both Demucs and NAM (header only).
FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.1
    GIT_SHALLOW    ON
    GIT_SUBMODULES ""
    PATCH_COMMAND  ${CMAKE_COMMAND} -DEIGEN_SRC=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchEigen.cmake
    UPDATE_DISCONNECTED ON
    SOURCE_SUBDIR  do-not-build)   # header only; never run Eigen's own CMake

# demucs.cpp: C++ inference for Meta's Demucs v4 hybrid transformer (6-stem model).
FetchContent_Declare(demucs
    GIT_REPOSITORY https://github.com/sevagh/demucs.cpp.git
    GIT_TAG        f1206e9adeea103aef4a636b9e62297cf1f8e34e
    GIT_SUBMODULES ""              # its vendored deps (eigen, pytorch demucs, gtest...) aren't needed
    SOURCE_SUBDIR  do-not-build)   # we compile its sources ourselves

# Neural Amp Modeler core: real-time inference of .nam amp captures.
FetchContent_Declare(nam
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        v0.6.0
    GIT_SHALLOW    ON
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  do-not-build)

# Signalsmith Stretch: high quality time-stretch / pitch-shift for practice speed + transpose.
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    ON
    GIT_SUBMODULES "")

# r8brain: high quality sample-rate conversion (file -> 44.1 kHz for Demucs, stems -> device rate).
FetchContent_Declare(r8brain
    GIT_REPOSITORY https://github.com/avaneev/r8brain-free-src.git
    GIT_TAG        cb2abb9977efe2471979b380ed95daa56ab4fdb9   # header-only (no r8bbase.cpp needed)
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  do-not-build)

# TinySoundFont: SoundFont (.sf2) player behind the Studio's "Sound Library" instrument.
FetchContent_Declare(tinysoundfont
    GIT_REPOSITORY https://github.com/schellingb/TinySoundFont.git
    GIT_TAG        853a0a171759f1ddba0de1442133a75912bbeffa
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  do-not-build)

# CLAP: the official (MIT) headers, for hosting CLAP plugins in the Studio.
FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.9
    GIT_SHALLOW    ON
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  do-not-build)

FetchContent_MakeAvailable(eigen demucs nam signalsmith-stretch r8brain tinysoundfont clap)

set(EIGEN_DIR  "${eigen_SOURCE_DIR}")
if(FETCHCONTENT_SOURCE_DIR_EIGEN)   # local override: PATCH_COMMAND doesn't run, so patch here (idempotent)
    execute_process(COMMAND ${CMAKE_COMMAND} -DEIGEN_SRC=${EIGEN_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchEigen.cmake)
endif()
set(DEMUCS_DIR "${demucs_SOURCE_DIR}")
set(NAM_DIR    "${nam_SOURCE_DIR}")
set(R8B_DIR    "${r8brain_SOURCE_DIR}")
set(TSF_DIR    "${tinysoundfont_SOURCE_DIR}")
set(CLAP_DIR   "${clap_SOURCE_DIR}/include")

# ---- Common optimisation flags for the heavy maths libraries -----------------
function(wis_fast_math target)
    if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # clang-cl: same "safe" fast-math subset as GCC below. Full /fp:fast (-ffast-math) assumes no
        # NaN/Inf and breaks Demucs' numerics.
        target_compile_options(${target} PRIVATE /O2 /bigobj /Zc:__cplusplus /utf-8
            /clang:-O3 /clang:-fno-signed-zeros /clang:-freciprocal-math /clang:-fno-math-errno -Wno-everything)
        if(WIS_AVX2)
            target_compile_options(${target} PRIVATE /arch:AVX2 /clang:-mfma)
        endif()
        target_compile_definitions(${target} PRIVATE _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /O2 /Ob2 /Oi /Ot /fp:fast /bigobj /permissive- /Zc:__cplusplus /utf-8)
        if(WIS_AVX2)
            target_compile_options(${target} PRIVATE /arch:AVX2)
        endif()
        target_compile_definitions(${target} PRIVATE _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-O3> -fno-signed-zeros -freciprocal-math -w)
        if(WIS_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            target_compile_options(${target} PRIVATE -mavx2 -mfma)
        endif()
    endif()
endfunction()

# ---- demucs library ----------------------------------------------------------
file(GLOB DEMUCS_SOURCES "${DEMUCS_DIR}/src/*.cpp")
add_library(wis_demucs STATIC ${DEMUCS_SOURCES})
target_include_directories(wis_demucs SYSTEM PUBLIC "${EIGEN_DIR}" "${DEMUCS_DIR}/src")
target_compile_definitions(wis_demucs PUBLIC EIGEN_MPL2_ONLY NDEBUG)
wis_fast_math(wis_demucs)

# ---- NAM library -------------------------------------------------------------
file(GLOB NAM_SOURCES "${NAM_DIR}/NAM/*.cpp" "${NAM_DIR}/NAM/wavenet/*.cpp")
add_library(wis_nam STATIC ${NAM_SOURCES})
target_include_directories(wis_nam SYSTEM PUBLIC "${EIGEN_DIR}" "${NAM_DIR}" "${NAM_DIR}/Dependencies/nlohmann")
target_compile_definitions(wis_nam PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
wis_fast_math(wis_nam)

# ---- JUCE --------------------------------------------------------------------
FetchContent_MakeAvailable(juce)

# Sources shared by the GUI app and the command line tools (no GUI code here).
set(WIS_ENGINE_SOURCES
    Source/Separation/AudioFileLoader.cpp
    Source/Separation/ModelManager.cpp
    Source/Separation/StemSeparator.cpp
    Source/Separation/VocalSplitter.cpp
    Source/Library/SongLibrary.cpp
    Source/Rig/AmpModels.cpp
    Source/Rig/CabSim.cpp
    Source/Rig/Convolver.cpp
    Source/Rig/NamAmp.cpp
    Source/Rig/RigProcessor.cpp
    Source/Rig/Tuner.cpp
    Source/Engine/AudioEngine.cpp
    Source/Engine/StemPlayer.cpp
    Source/Engine/Recorder.cpp
    # Studio (DAW)
    Source/Daw/Model/Project.cpp
    Source/Daw/Model/DrumPatterns.cpp
    Source/Daw/Model/TempoDetect.cpp
    Source/Daw/Model/MidiLoops.cpp
    Source/Daw/Engine/AudioCache.cpp
    Source/Daw/Engine/DawEngine.cpp
    Source/Daw/Plugins/BuiltinProcessor.cpp
    Source/Daw/Plugins/BuiltinEffects.cpp
    Source/Daw/Plugins/BuiltinRegistry.cpp
    Source/Daw/Plugins/TestEffects.cpp
    Source/Daw/Plugins/NativeWindow.cpp
    Source/Daw/Plugins/Vst2Format.cpp
    Source/Daw/Plugins/Vst2Bridge.cpp
    Source/Daw/Plugins/ClapFormat.cpp
    Source/Daw/Plugins/PluginHost.cpp
    Source/Daw/Plugins/CreativeEffects.cpp
    Source/Daw/Plugins/MidiEffects.cpp
    Source/Daw/Plugins/VocalTune.cpp
    Source/Daw/Plugins/Looper.cpp
    Source/Daw/Plugins/GuitarPedals.cpp
    Source/Daw/Instruments/SoundFontInstrument.cpp
    Source/Daw/Instruments/StudioSynth.cpp
    Source/Daw/Instruments/DrumSynth.cpp
    Source/Daw/Instruments/VintageRhythms.cpp
    Source/Daw/Instruments/RhythmBox.cpp
    Source/Daw/Instruments/HomeKeys.cpp
    Source/Daw/Instruments/Sampler.cpp
    Source/Daw/Instruments/BeatLab.cpp
    Source/Daw/Instruments/RoomIr.cpp
    Source/Daw/Instruments/PianoRoom.cpp
    Source/Daw/Instruments/Junkyard.cpp
    Source/Daw/Instruments/Whistler.cpp
    Source/Daw/Model/JunkLoops.cpp
    Source/Daw/Model/VocalChains.cpp)

set(WIS_JUCE_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_AU=0
    WIS_VERSION_STRING="${PROJECT_VERSION}")

set(WIS_JUCE_MODULES
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_cryptography
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra)

function(wis_configure_target target)
    target_include_directories(${target} PRIVATE Source)
    target_include_directories(${target} SYSTEM PRIVATE "${R8B_DIR}" "${TSF_DIR}" "${CLAP_DIR}")
    target_compile_definitions(${target} PRIVATE ${WIS_JUCE_DEFS})
    target_link_libraries(${target} PRIVATE wis_demucs wis_nam signalsmith-stretch ${WIS_JUCE_MODULES}
        juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
    if(MSVC)
        # no fast-math here: the real-time code relies on std::isfinite() as a safety net
        target_compile_options(${target} PRIVATE /bigobj /utf-8 /Zc:__cplusplus)
        if(WIS_AVX2)
            target_compile_options(${target} PRIVATE /arch:AVX2)
        endif()
        target_compile_definitions(${target} PRIVATE _USE_MATH_DEFINES NOMINMAX)
    elseif(WIS_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(${target} PRIVATE -mavx2 -mfma)
    endif()
    if(WIS_ASIO_SDK_DIR AND WIN32)
        target_include_directories(${target} PRIVATE "${WIS_ASIO_SDK_DIR}/common")
        target_compile_definitions(${target} PRIVATE JUCE_ASIO=1)
    endif()
endfunction()

# ---- GUI application -------------------------------------------------------------
if(WIS_BUILD_APP)
    juce_add_gui_app(WOMANINSTEM
        PRODUCT_NAME          "WOMANINSTEM"
        COMPANY_NAME          "WOMANINSTEM"
        BUNDLE_ID             "com.womaninstem.app"
        VERSION               "${PROJECT_VERSION}"
        ICON_BIG              "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "WOMANINSTEM needs audio input to hear your instrument.")

    target_sources(WOMANINSTEM PRIVATE
        Source/Main.cpp
        Source/UI/MainComponent.cpp
        Source/UI/LookAndFeel.cpp
        Source/UI/WaveformView.cpp
        Source/UI/StemMixer.cpp
        Source/UI/RigPanel.cpp
        Source/UI/KeysPanel.cpp
        Source/UI/PedalboardStrip.cpp
        Source/UI/TunerView.cpp
        Source/UI/SeparationOverlay.cpp
        Source/UI/LibraryView.cpp
        Source/UI/AppShell.cpp
        Source/UI/Scope/ScopeRenderer.cpp
        Source/UI/Scope/ScopeWindow.cpp
        Source/UI/Studio/TrackHeader.cpp
        Source/UI/Studio/ArrangementView.cpp
        Source/UI/Studio/PianoRoll.cpp
        Source/UI/Studio/MixerView.cpp
        Source/UI/Studio/BrowserPanel.cpp
        Source/UI/Studio/ControlBar.cpp
        Source/UI/Studio/PluginEditors.cpp
        Source/UI/Studio/InstrumentEditors.cpp
        Source/UI/Studio/BeatLabEditor.cpp
        Source/UI/Studio/VoiceEditors.cpp
        Source/UI/Studio/StudioPage.cpp
        ${WIS_ENGINE_SOURCES})
    wis_configure_target(WOMANINSTEM)
    target_compile_definitions(WOMANINSTEM PRIVATE
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:WOMANINSTEM,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:WOMANINSTEM,JUCE_VERSION>")
endif()

# ---- Command line tools --------------------------------------------------------
if(WIS_BUILD_TOOLS)
    # stemsplit: headless separation (batch use, and the CI smoke test)
    juce_add_console_app(stemsplit PRODUCT_NAME "stemsplit")
    target_sources(stemsplit PRIVATE Tools/StemSplitCli.cpp ${WIS_ENGINE_SOURCES})
    wis_configure_target(stemsplit)

    # dawtest: the Studio engine end to end (instruments, clips, recording, export, MIDI files, undo)
    juce_add_console_app(dawtest PRODUCT_NAME "dawtest")
    target_sources(dawtest PRIVATE Tools/DawTest.cpp ${WIS_ENGINE_SOURCES})
    wis_configure_target(dawtest)

    # tiny VST2 and CLAP plugins that dawtest loads through our own plugin hosts
    add_library(wis_test_vst2 MODULE Tools/TestVst2Plugin.cpp)
    add_library(wis_test_clap MODULE Tools/TestClapPlugin.cpp)
    target_include_directories(wis_test_clap SYSTEM PRIVATE "${CLAP_DIR}")
    set_target_properties(wis_test_clap PROPERTIES PREFIX "" SUFFIX ".clap")
    set_target_properties(wis_test_vst2 wis_test_clap PROPERTIES CXX_VISIBILITY_PRESET hidden)
    add_dependencies(dawtest wis_test_vst2 wis_test_clap)
    target_compile_definitions(dawtest PRIVATE
        WIS_TEST_VST2="$<TARGET_FILE:wis_test_vst2>"
        WIS_TEST_CLAP="$<TARGET_FILE:wis_test_clap>")

    # rigtest: renders test signals through every rig block and checks the output is sane
    juce_add_console_app(rigtest PRODUCT_NAME "rigtest")
    target_sources(rigtest PRIVATE Tools/RigTest.cpp ${WIS_ENGINE_SOURCES})
    wis_configure_target(rigtest)
endif()
