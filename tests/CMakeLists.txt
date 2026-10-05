juce_add_console_app(DumbleTests PRODUCT_NAME "DumbleTests")

target_sources(DumbleTests PRIVATE
    Main.cpp
    AllocationGuard.cpp
    ToneStackTests.cpp
    TriodeTests.cpp
    EngineTests.cpp
    ${PROJECT_SOURCE_DIR}/source/dsp/AmpEngine.cpp)

target_include_directories(DumbleTests PRIVATE ${PROJECT_SOURCE_DIR}/source)

target_compile_definitions(DumbleTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_UNIT_TESTS=0)

target_link_libraries(DumbleTests
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

dumble_apply_dsp_flags(DumbleTests)

enable_testing()
add_test(NAME DumbleTests COMMAND DumbleTests)
