cmake_minimum_required(VERSION 3.25)

project(VST3MCPWrapper
    VERSION 0.1.0
    LANGUAGES CXX
)

# OBJCXX needed for .mm files on macOS only
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Fetch VST3 SDK ---
include(FetchContent)

FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.7.12_build_20
    GIT_SHALLOW TRUE
)

set(SMTG_ADD_VSTGUI OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_PLUGINS_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_HOSTING_SAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(vst3sdk)

# --- Fetch cpp-mcp ---
FetchContent_Declare(cpp_mcp
    GIT_REPOSITORY https://github.com/hkr04/cpp-mcp.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    PATCH_COMMAND ${CMAKE_COMMAND} -DFILE=${FETCHCONTENT_BASE_DIR}/cpp_mcp-src/include/mcp_message.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_mcp_version.cmake
        && ${CMAKE_COMMAND} -DFILE=${FETCHCONTENT_BASE_DIR}/cpp_mcp-src/src/mcp_server.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_mcp_server.cmake
)

FetchContent_MakeAvailable(cpp_mcp)

# On Linux, the plugin is a shared object (.so) — all static libraries
# linked into it must be compiled with position-independent code.
if(NOT APPLE)
    set_target_properties(mcp PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# --- Plugin target ---
smtg_add_vst3plugin(VST3MCPWrapper
    source/pluginids.h
    source/messageids.h
    source/stateformat.h
    source/dispatcher.h
    source/version.h
    source/hostedplugin.h
    source/hostedplugin.cpp
    source/processor.h
    source/processor.cpp
    source/controller.h
    source/controller.cpp
    source/wrapperview.h
    source/factory.cpp
)

# Platform-specific module entry points
if(APPLE)
    # macmain.cpp provides bundleEntry/bundleExit exports required on macOS
    target_sources(VST3MCPWrapper PRIVATE
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp
    )
else()
    # linuxmain.cpp provides ModuleEntry/ModuleExit exports required on Linux
    target_sources(VST3MCPWrapper PRIVATE
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp
    )
endif()

# Add platform-specific module loading, view, and dispatch
if(APPLE)
    target_sources(VST3MCPWrapper PRIVATE
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
        source/wrapperview.mm
        source/dispatcher_mac.mm
    )
    set_source_files_properties(
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
        source/wrapperview.mm
        source/dispatcher_mac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
    target_link_libraries(VST3MCPWrapper PRIVATE "-framework Foundation" "-framework CoreFoundation" "-framework AppKit")
else()
    target_sources(VST3MCPWrapper PRIVATE
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_linux.cpp
        source/wrapperview_linux.cpp
        source/dispatcher_linux.cpp
    )
endif()

target_link_libraries(VST3MCPWrapper
    PRIVATE
        sdk
        sdk_hosting
        mcp
)

target_compile_options(VST3MCPWrapper PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_include_directories(VST3MCPWrapper
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${cpp_mcp_SOURCE_DIR}/include
        ${cpp_mcp_SOURCE_DIR}/common
)

if(APPLE)
    smtg_target_set_bundle(VST3MCPWrapper
        BUNDLE_IDENTIFIER "com.vst3mcpwrapper.plugin"
        COMPANY_NAME "VST3MCPWrapper"
    )
    # Generate PkgInfo for proper bundle recognition
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo" "BNDL????")
    add_custom_command(TARGET VST3MCPWrapper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo"
            "$<TARGET_BUNDLE_DIR:VST3MCPWrapper>/Contents/PkgInfo"
        COMMENT "Creating PkgInfo"
    )
    # Ad-hoc sign the bundle so hosts with hardened runtime (e.g. Ableton) accept it
    add_custom_command(TARGET VST3MCPWrapper POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:VST3MCPWrapper>"
        COMMENT "Ad-hoc code signing VST3MCPWrapper"
    )
    set_target_properties(VST3MCPWrapper PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vst3mcpwrapper.plugin"
        MACOSX_BUNDLE_BUNDLE_NAME "VST3MCPWrapper"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resource/Info.plist.in"
    )
endif()

# --- Tests ---
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
