cmake_minimum_required(VERSION 3.13)

# Firmware by default. -DPDG_HOST=ON builds the desktop simulator and the
# unit tests instead, with the host compiler.
option(PDG_HOST "Build the desktop simulator and tests instead of firmware" OFF)

if(NOT PDG_HOST)
    if(NOT PICO_BOARD)
        set(PICO_BOARD pico)
    endif()
    include(cmake/pico_sdk_import.cmake)
endif()

project(pico_display_games C CXX ASM)
set(CMAKE_C_STANDARD 11)

# Bad Apple's frames are not in git. tools/encode_bad_apple.py writes them to
# data/bad_apple.bin; without that file the app explains how to make it.
set(BAD_APPLE_BIN "${CMAKE_CURRENT_SOURCE_DIR}/data/bad_apple.bin" CACHE FILEPATH "Encoded Bad Apple frames")
if(EXISTS "${BAD_APPLE_BIN}")
    set(BAD_APPLE_INCBIN ".incbin \"${BAD_APPLE_BIN}\"")
    message(STATUS "Bad Apple: ${BAD_APPLE_BIN}")
else()
    set(BAD_APPLE_INCBIN "")
    message(STATUS "Bad Apple: no data/bad_apple.bin, the app will show how to make it")
endif()
configure_file(src/apps/bad_apple_data.S.in ${CMAKE_CURRENT_BINARY_DIR}/bad_apple_data.S)
if(EXISTS "${BAD_APPLE_BIN}")
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/bad_apple_data.S PROPERTIES OBJECT_DEPENDS "${BAD_APPLE_BIN}")
endif()

file(GLOB APP_SOURCES CONFIGURE_DEPENDS src/apps/*.c)
set(GAME_SOURCES
    src/core/gfx.c
    src/core/save.c
    src/assets/assets.c
    ${APP_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/bad_apple_data.S
)

if(NOT PDG_HOST)
    pico_sdk_init()
    add_executable(pico_display_games src/main.c ${GAME_SOURCES} src/platform/hal_pico.c)
    target_compile_options(pico_display_games PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(pico_display_games
        pico_stdlib pico_rand pico_flash
        hardware_spi hardware_dma hardware_pwm hardware_flash hardware_watchdog)
    pico_enable_stdio_usb(pico_display_games 1)
    pico_enable_stdio_uart(pico_display_games 0)
    pico_set_program_name(pico_display_games "Pico Display Games")
    pico_set_program_description(pico_display_games "Eight games and toys for the Pimoroni Pico Display Pack")
    pico_set_program_url(pico_display_games "https://github.com/kirbycope/raspberry-pi-pico-display-games")
    pico_add_extra_outputs(pico_display_games)
else()
    find_package(SDL2 QUIET)
    add_executable(pdg_sim src/main.c ${GAME_SOURCES} src/platform/hal_sim.c)
    target_compile_definitions(pdg_sim PRIVATE PDG_SIM)
    target_compile_options(pdg_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(SDL2_FOUND)
        target_compile_definitions(pdg_sim PRIVATE PDG_SDL)
        target_link_libraries(pdg_sim PRIVATE SDL2::SDL2)
    else()
        message(STATUS "SDL2 not found: the simulator runs scripted (PDG_SCRIPT) only")
    endif()

    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.c)
    add_executable(pdg_tests ${TEST_SOURCES} ${GAME_SOURCES} src/platform/hal_sim.c)
    target_compile_definitions(pdg_tests PRIVATE PDG_SIM PDG_TEST)
    target_include_directories(pdg_tests PRIVATE src)
    enable_testing()
    add_test(NAME unit COMMAND pdg_tests)
endif()
