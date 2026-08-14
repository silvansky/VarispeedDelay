cmake_minimum_required(VERSION 3.22)
project(VarispeedDelay VERSION 0.1.0)

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
)

target_sources(VarispeedDelay PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_compile_definitions(VarispeedDelay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

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
