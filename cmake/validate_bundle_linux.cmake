# Validates the Linux VST3 bundle layout.
# Usage: cmake -DSO_FILE=<path to .so> -P validate_bundle_linux.cmake

if(NOT DEFINED SO_FILE)
    message(FATAL_ERROR "SO_FILE not defined")
endif()

if(NOT EXISTS "${SO_FILE}")
    message(FATAL_ERROR "Shared library not found: ${SO_FILE}")
endif()

# Derive bundle directory (SO is at .vst3/Contents/x86_64-linux/name.so)
get_filename_component(ARCH_DIR "${SO_FILE}" DIRECTORY)
get_filename_component(CONTENTS_DIR "${ARCH_DIR}" DIRECTORY)
get_filename_component(BUNDLE_DIR "${CONTENTS_DIR}" DIRECTORY)
get_filename_component(ARCH_NAME "${ARCH_DIR}" NAME)
get_filename_component(BUNDLE_NAME "${BUNDLE_DIR}" NAME)

# Verify .vst3 bundle directory name
if(NOT BUNDLE_NAME MATCHES "\\.vst3$")
    message(FATAL_ERROR "Bundle directory should end with .vst3, got '${BUNDLE_NAME}'")
endif()

# Verify Contents directory exists
get_filename_component(CONTENTS_NAME "${CONTENTS_DIR}" NAME)
if(NOT CONTENTS_NAME STREQUAL "Contents")
    message(FATAL_ERROR "Expected 'Contents' directory, got '${CONTENTS_NAME}'")
endif()

# Verify architecture directory
if(NOT ARCH_NAME STREQUAL "x86_64-linux")
    message(FATAL_ERROR "Expected architecture directory 'x86_64-linux', got '${ARCH_NAME}'")
endif()

# Verify .so filename matches plugin name
get_filename_component(SO_NAME "${SO_FILE}" NAME)
if(NOT SO_NAME STREQUAL "VST3MCPWrapper.so")
    message(FATAL_ERROR "Expected VST3MCPWrapper.so, got ${SO_NAME}")
endif()

# Verify it's an ELF shared object
execute_process(
    COMMAND file "${SO_FILE}"
    OUTPUT_VARIABLE FILE_OUTPUT
    RESULT_VARIABLE FILE_RESULT
)
if(NOT FILE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to run 'file' command on ${SO_FILE}")
endif()
if(NOT FILE_OUTPUT MATCHES "ELF.*shared object")
    message(FATAL_ERROR "Not a valid ELF shared library: ${FILE_OUTPUT}")
endif()

message(STATUS "Linux VST3 bundle layout validated:")
message(STATUS "  ${BUNDLE_NAME}/")
message(STATUS "    Contents/")
message(STATUS "      x86_64-linux/")
message(STATUS "        VST3MCPWrapper.so [ELF 64-bit shared object]")
