cmake_minimum_required(VERSION 3.22)
project(EpsilonCarve VERSION 0.5.2 LANGUAGES C CXX)

# macOS: 12.0 target and JUCE 9.0.5 (bundled in ./JUCE). Older targets failed in the VST3 manifest helper.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

add_subdirectory(JUCE)
set(carve_formats VST3)
if(APPLE)
    list(APPEND carve_formats AU)
endif()

juce_add_plugin(EpsilonCarve
    COMPANY_NAME "Epsilon Sounds"
    BUNDLE_ID com.epsilonsounds.carve
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE EpsS
    PLUGIN_CODE EpCv
    VST3_CATEGORIES Fx EQ
    AU_MAIN_TYPE kAudioUnitType_Effect
    FORMATS ${carve_formats}
    PRODUCT_NAME "Epsilon Carve"
    DESCRIPTION "Collision-aware sidechain EQ"
)
juce_generate_juce_header(EpsilonCarve)
juce_add_binary_data(CarveAssets SOURCES
    Resources/BarlowSemiCondensed-Medium.ttf
    Resources/BarlowSemiCondensed-SemiBold.ttf
    Resources/BarlowSemiCondensed-Bold.ttf
)
set_target_properties(CarveAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(EpsilonCarve PRIVATE
    Source/CarveEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)
target_compile_features(EpsilonCarve PRIVATE cxx_std_17)
target_compile_definitions(EpsilonCarve PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(EpsilonCarve PRIVATE
    CarveAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Engine tests (no JUCE needed): cmake --build build --target CarveEngineTests && ctest
option(CARVE_BUILD_TESTS "Build the DSP engine tests" ON)
if(CARVE_BUILD_TESTS)
    enable_testing()
    add_executable(CarveEngineTests Tests/EngineTests.cpp Source/CarveEngine.cpp)
    target_include_directories(CarveEngineTests PRIVATE Source)
    target_compile_features(CarveEngineTests PRIVATE cxx_std_17)
    add_test(NAME CarveEngineTests COMMAND CarveEngineTests)
endif()
