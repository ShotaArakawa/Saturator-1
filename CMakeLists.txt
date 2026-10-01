cmake_minimum_required(VERSION 3.22)

project(Saturator1 VERSION 1.0.0)

add_subdirectory(../JUCE JUCE)

juce_add_plugin(Saturator1
    COMPANY_NAME "AudioN"
    PLUGIN_MANUFACTURER_CODE AudN
    PLUGIN_CODE Sa01
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Saturator-1"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

juce_generate_juce_header(Saturator1)

target_sources(Saturator1
    PRIVATE
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp)

target_compile_definitions(Saturator1
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Saturator1
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
