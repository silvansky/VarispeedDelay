cmake_minimum_required(VERSION 3.22)
project(VarispeedDelay VERSION 0.1.3)

add_subdirectory(libs/JUCE)

juce_add_plugin(VarispeedDelay
    COMPANY_NAME "Valentine Silvansky"
    BUNDLE_ID "com.silvansky.VarispeedDelay"
    PLUGIN_MANUFACTURER_CODE VSil
    PLUGIN_CODE Vspd
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "VarispeedDelay"
    NEEDS_MIDI_INPUT FALSE
    IS_SYNTH FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "VarispeedDelay needs audio input to process live signal."
)

set(VSPD_ENGINE_SOURCES
    src/DelayEngine.cpp
    src/GraphicEQ.cpp
)

target_sources(VarispeedDelay PRIVATE
    ${VSPD_ENGINE_SOURCES}
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/LookAndFeel.cpp
    src/Presets.cpp
    src/RepetitionView.cpp
)

# Debug builds always show the preset authoring row; this opts a release build in too.
option(VSPD_PRESET_AUTHORING "Show the preset authoring row in release plugin builds" OFF)
if(VSPD_PRESET_AUTHORING)
    target_compile_definitions(VarispeedDelay PRIVATE VSPD_PRESET_AUTHORING=1)
endif()

target_compile_definitions(VarispeedDelay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# Factory presets are authored in the standalone; CMake globs at configure time, so a new
# preset file needs a `cmake ..` before it is embedded.
file(GLOB VSPD_PRESET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/presets/*.xml")
if(VSPD_PRESET_FILES)
    juce_add_binary_data(VarispeedDelayPresets SOURCES ${VSPD_PRESET_FILES})
    target_link_libraries(VarispeedDelay PRIVATE VarispeedDelayPresets)
    target_compile_definitions(VarispeedDelay PRIVATE VSPD_HAS_PRESETS=1)
endif()

add_custom_target(VarispeedDelay_BuildDate ALL
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildDate.h.in
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/BuildDate.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildDate.cmake
    COMMENT "Updating build date"
)
add_dependencies(VarispeedDelay VarispeedDelay_BuildDate)
target_include_directories(VarispeedDelay PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(VarispeedDelay PUBLIC cxx_std_20)

target_link_libraries(VarispeedDelay
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---- engine unit tests (must not depend on AudioProcessor) -------------------
juce_add_console_app(VarispeedDelayTests PRODUCT_NAME "VarispeedDelayTests")

target_sources(VarispeedDelayTests PRIVATE
    ${VSPD_ENGINE_SOURCES}
    tests/EngineTests.cpp
)

target_include_directories(VarispeedDelayTests PRIVATE src)
target_compile_features(VarispeedDelayTests PRIVATE cxx_std_20)

target_compile_definitions(VarispeedDelayTests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(VarispeedDelayTests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---- preset / parameter tests (need the APVTS, so they link the shared code) -
add_executable(VarispeedDelayPresetTests tests/PresetTests.cpp)
add_dependencies(VarispeedDelayPresetTests VarispeedDelay_BuildDate)

target_include_directories(VarispeedDelayPresetTests PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(VarispeedDelayPresetTests PRIVATE cxx_std_20)

target_link_libraries(VarispeedDelayPresetTests
    PRIVATE
        VarispeedDelay
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

enable_testing()
add_test(NAME engine COMMAND VarispeedDelayTests)
add_test(NAME presets COMMAND VarispeedDelayPresetTests)
