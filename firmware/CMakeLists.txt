cmake_minimum_required(VERSION 3.13)

# --- configuration ---------------------------------------------------------
# Default script and modules. They are embedded into the firmware and copied
# onto the USB drive when it is formatted (first boot); afterwards the copies on
# the drive are what runs, and the embedded ones only serve as a fallback.
set(PICOPERL_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/main.pl" CACHE FILEPATH "Default /main.pl")
set(PICOPERL_EXTRA_SCRIPTS "" CACHE STRING "Additional .pl/.pm files to embed (semicolon separated)")
# Size of the FAT filesystem at the end of the flash (RP2040 Pico: 2 MiB flash)
set(PICOPERL_FS_SIZE "1048576" CACHE STRING "Flash filesystem size in bytes")
set(PICO_BOARD pico CACHE STRING "Board type")
# ON: format the drive and rewrite the built-in files on every boot (reproducible test setups)
option(PICOPERL_FS_RESET "Recreate the drive from the built-in files on every boot" OFF)
# ON: also embed workshop/*/main.pl; they appear as /workshop/... on the drive,
# so an example can be tried by copying it over /main.pl on the device
option(PICOPERL_EMBED_WORKSHOP "Embed the workshop examples into the drive" OFF)

include(pico_sdk_import.cmake)

project(picoperl C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(PP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(PP_SRC
    ${PP_ROOT}/src/pp_value.c
    ${PP_ROOT}/src/pp_parse.c
    ${PP_ROOT}/src/pp_eval.c
    ${PP_ROOT}/src/pp_builtin.c
    ${PP_ROOT}/src/pp_keycodes.c
    ${PP_ROOT}/src/pp_gfx.c
    ${PP_ROOT}/src/pp_fonts.c
    ${PP_ROOT}/src/pp_tone.c
    ${PP_ROOT}/src/pp_regex.c
    ${PP_ROOT}/src/pp_match.c
)

# Embed the scripts as C string literals (perl is only needed on the build host).
file(GLOB_RECURSE PP_LIB_FILES ${PP_ROOT}/lib/*.pm ${PP_ROOT}/lib/*.pl)
set(PP_SCRIPTS_C ${CMAKE_CURRENT_BINARY_DIR}/pp_scripts.c)
set(PP_EMBED_FILES ${PICOPERL_SCRIPT} ${PP_LIB_FILES} ${PICOPERL_EXTRA_SCRIPTS})
if (PICOPERL_EMBED_WORKSHOP)
    file(GLOB PP_WORKSHOP_FILES ${PP_ROOT}/workshop/*/main.pl)
    list(APPEND PP_EMBED_FILES ${PP_WORKSHOP_FILES})
endif()
add_custom_command(
    OUTPUT ${PP_SCRIPTS_C}
    COMMAND perl ${PP_ROOT}/tools/embed.pl -o ${PP_SCRIPTS_C} -b ${PP_ROOT}/lib -r ${PP_ROOT} ${PP_EMBED_FILES}
    DEPENDS ${PP_EMBED_FILES} ${PP_ROOT}/tools/embed.pl
    COMMENT "Embedding Perl scripts"
    VERBATIM
)

add_executable(picoperl
    main.c
    usb_descriptors.c
    flashfs.c
    fatfs/ff.c
    fatfs/ffunicode.c
    ${PP_SRC}
    ${PP_SCRIPTS_C}
)

target_include_directories(picoperl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/fatfs
    ${PP_ROOT}/src
)

target_compile_definitions(picoperl PRIVATE
    PP_STACK_BYTES=0xC000       # 48 KiB stack for the interpreter, allocated from the heap at boot
    PP_RE_MAX_STACK=2048        # regex backtrack entries (16 bytes each)
    PP_RE_MAX_STEPS=1000000     # give up on catastrophic backtracking after this many VM steps
    PP_MAX_DEPTH=200            # perl call depth limit (the C stack is also checked at run time)
    PICOPERL_FS_SIZE=${PICOPERL_FS_SIZE}
    # malloc returns NULL when memory runs out (the SDK's default is to panic and hang):
    # picoperl then reports "out of memory" and restarts (pp_host_fatal)
    PICO_MALLOC_PANIC=0
    PICOPERL_FS_RESET=$<BOOL:${PICOPERL_FS_RESET}>
)

target_link_libraries(picoperl
    pico_stdlib
    pico_bootrom
    hardware_flash
    hardware_pio
    hardware_adc
    hardware_i2c
    hardware_pwm
    tinyusb_device
    tinyusb_board
)

# Use newlib's printf: the SDK's compact printf limits float precision (%.15g)
pico_set_printf_implementation(picoperl compiler)

# print() goes to the USB CDC serial port; also mirror to UART0 (GP0/GP1) for probes
pico_enable_stdio_uart(picoperl 1)
pico_enable_stdio_usb(picoperl 0)

pico_add_extra_outputs(picoperl)
