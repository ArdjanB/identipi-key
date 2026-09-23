# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Pico 2 W = RP2350 + CYW43. Needs Pico SDK 2.0 or newer.
set(PICO_BOARD pico2_w CACHE STRING "Board type")

if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
    # Local copy, or one the VS Code extension generated for us
    include(pico_sdk_import.cmake)
elseif(DEFINED ENV{PICO_SDK_PATH} AND
       EXISTS $ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    # Fall back to the copy inside the SDK itself
    message(STATUS "Using pico_sdk_import.cmake from $ENV{PICO_SDK_PATH}")
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
    message(FATAL_ERROR
        "Cannot find pico_sdk_import.cmake.\n"
        "Either set the PICO_SDK_PATH environment variable, or copy the "
        "file here:\n"
        "  cp $ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake .")
endif()

project(identipi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Milestone 5: config-blob encryption needs mbedtls's AES-GCM. This must be
# set before pico_sdk_init() -- the SDK's pico_mbedtls/CMakeLists.txt only
# honours it if it's already a defined variable by the time that
# subdirectory is processed, which happens inside pico_sdk_init().
#
# Must be an ABSOLUTE path, not a bare filename: #include MBEDTLS_CONFIG_FILE
# happens from inside mbedtls's own headers (lib/mbedtls/include/mbedtls/...),
# and quote-includes check the *including file's own directory* before any
# -I path -- which is exactly where mbedtls's stock, fully-featured
# mbedtls_config.h template also lives. A bare filename silently resolved to
# that one instead of ours, defining a pile of features (entropy, timing...)
# our bare-metal build can't satisfy. An absolute path has no ambiguity.
set(PICO_MBEDTLS_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/mbedtls_config.h")

pico_sdk_init()

add_executable(identipi
    src/main.c
    src/display.c
    src/fp.c
    src/buttons.c
    src/hid.c
    src/config.c
    src/keyboard.c
    src/menu.c
    src/otp_key.c
)

target_include_directories(identipi PRIVATE src)

target_link_libraries(identipi
    pico_stdlib
    hardware_spi
    hardware_uart
    hardware_flash
    pico_rand
    pico_unique_id
    pico_bootrom
    tinyusb_device
    pico_mbedtls
)

# Milestone 2: TinyUSB owns the USB interface as a HID keyboard. USB CDC
# debug output (milestone 1) is off -- one interface, one job. printf()
# calls remain in the code but currently go nowhere.
pico_enable_stdio_usb(identipi 0)
pico_enable_stdio_uart(identipi 0)

# Stamp a fresh patch version + build timestamp into version.h on every
# build. This is a plain custom target (ALL, no tracked OUTPUT), so
# Ninja/Make reruns its command unconditionally each invocation, not just
# when some input changed.
#
# pico_sdk_init() resolves Python3 for its own codegen, but only inside its
# own internal scope -- Python3_EXECUTABLE doesn't propagate up to here, so
# it silently comes out empty and the command below would run the bare .py
# file with no interpreter. Resolve it again explicitly in this scope
# (cheap: CMake reuses the cache from pico_sdk_init()'s search).
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# BYPRODUCTS is what makes this actually work, not just add_dependencies()
# below: without it, Ninja computes its whole rebuild plan up front from
# on-disk mtimes *before* this target runs, so version.h's freshly written
# mtime is invisible to that plan and every .c file that includes it (via
# its already-recorded .d depfile) gets silently left stale -- version.h
# on disk shows the new version, but the linked .elf/.uf2 still has the
# previous one baked in. Declaring version.h as a BYPRODUCT registers it as
# a real graph output with restat semantics, so Ninja re-checks it *after*
# this step runs and correctly cascades the rebuild in the same invocation.
add_custom_target(bump_version ALL
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/bump_version.py
            ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h
    BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h
    COMMENT "Bumping firmware patch version and build time"
    VERBATIM
)
add_dependencies(identipi bump_version)

# Produces identipi.uf2 alongside the .elf
pico_add_extra_outputs(identipi)
