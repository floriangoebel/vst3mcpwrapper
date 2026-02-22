include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(VST3MCPWrapper_Tests
    test_placeholder.cpp
    test_utf16_conversion.cpp
    test_stateformat.cpp
    test_param_queue.cpp
    ${CMAKE_SOURCE_DIR}/source/hostedplugin.cpp
)

# Platform-specific module loading required by hostedplugin.cpp
if(APPLE)
    target_sources(VST3MCPWrapper_Tests PRIVATE
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
    )
    set_source_files_properties(
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
    target_link_libraries(VST3MCPWrapper_Tests PRIVATE
        "-framework Foundation" "-framework CoreFoundation" "-framework AppKit"
    )
endif()

target_link_libraries(VST3MCPWrapper_Tests
    PRIVATE
        sdk
        sdk_hosting
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

target_include_directories(VST3MCPWrapper_Tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/source
)

include(GoogleTest)
gtest_discover_tests(VST3MCPWrapper_Tests)
