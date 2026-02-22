include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(VST3MCPWrapper_Tests
    test_utf16_conversion.cpp
    test_stateformat.cpp
    test_param_queue.cpp
    test_hostedplugin.cpp
    test_mock_vst3.cpp
    test_processor_lifecycle.cpp
    test_processor_process.cpp
    test_processor_state.cpp
    test_mcp_param_tools.cpp
    test_mcp_plugin_tools.cpp
    test_message_routing.cpp
    test_shutdown.cpp
    test_dispatcher_linux.cpp
    test_mcp_server_integration.cpp
    test_plugin_scanning.cpp
    ${CMAKE_SOURCE_DIR}/source/hostedplugin.cpp
    ${CMAKE_SOURCE_DIR}/source/processor.cpp
)

# Platform-specific module loading and dispatch required by hostedplugin.cpp / dispatcher
if(APPLE)
    target_sources(VST3MCPWrapper_Tests PRIVATE
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
        ${CMAKE_SOURCE_DIR}/source/dispatcher_mac.mm
    )
    set_source_files_properties(
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
        ${CMAKE_SOURCE_DIR}/source/dispatcher_mac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
    target_link_libraries(VST3MCPWrapper_Tests PRIVATE
        "-framework Foundation" "-framework CoreFoundation" "-framework AppKit"
    )
else()
    target_sources(VST3MCPWrapper_Tests PRIVATE
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_linux.cpp
        ${CMAKE_SOURCE_DIR}/source/dispatcher_linux.cpp
    )
endif()

target_link_libraries(VST3MCPWrapper_Tests
    PRIVATE
        sdk
        sdk_hosting
        mcp
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

target_include_directories(VST3MCPWrapper_Tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/source
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${cpp_mcp_SOURCE_DIR}/include
        ${cpp_mcp_SOURCE_DIR}/common
)

target_compile_options(VST3MCPWrapper_Tests PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_dependencies(VST3MCPWrapper_Tests VST3MCPWrapper)
target_compile_definitions(VST3MCPWrapper_Tests PRIVATE
    "TEST_PLUGIN_SO_PATH=\"$<TARGET_FILE:VST3MCPWrapper>\""
)

include(GoogleTest)
gtest_discover_tests(VST3MCPWrapper_Tests)

# Validate Linux VST3 bundle layout via ctest
if(NOT APPLE)
    add_test(
        NAME validate_vst3_bundle_layout
        COMMAND ${CMAKE_COMMAND}
            -DSO_FILE=$<TARGET_FILE:VST3MCPWrapper>
            -P ${CMAKE_SOURCE_DIR}/cmake/validate_bundle_linux.cmake
    )
endif()
