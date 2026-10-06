juce_add_console_app(DumbleTests PRODUCT_NAME "DumbleTests")

target_sources(DumbleTests PRIVATE
    Main.cpp
    AllocationGuard.cpp
    TriodeTests.cpp
    EngineTests.cpp
    Sss002Tests.cpp
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

#===============================================================================
# VOX AC30C2 DSP tests
if(AMP_BUILD_AC30)
    juce_add_console_app(Ac30Tests PRODUCT_NAME "Ac30Tests")

    target_sources(Ac30Tests PRIVATE
        Ac30Main.cpp
        AllocationGuard.cpp
        Ac30Tests.cpp
        ${AC30_DSP_SOURCES})

    target_include_directories(Ac30Tests PRIVATE ${PROJECT_SOURCE_DIR}/ac30 ${PROJECT_SOURCE_DIR}/source)

    target_compile_definitions(Ac30Tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_UNIT_TESTS=0)

    target_link_libraries(Ac30Tests
        PRIVATE
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    dumble_apply_dsp_flags(Ac30Tests)
    add_test(NAME Ac30Tests COMMAND Ac30Tests)
endif()
