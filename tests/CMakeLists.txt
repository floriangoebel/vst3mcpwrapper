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
)

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
