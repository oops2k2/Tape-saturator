cmake_minimum_required(VERSION 3.22)
project(TapeSat VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(TapeSat
    COMPANY_NAME "DTStudio"
    PLUGIN_MANUFACTURER_CODE DTSt
    PLUGIN_CODE Tpst
    FORMATS VST3 Standalone
    PRODUCT_NAME "TapeSat"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(TapeSat PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_compile_features(TapeSat PRIVATE cxx_std_17)
target_compile_definitions(TapeSat PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TapeSat PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
