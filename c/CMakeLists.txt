cmake_minimum_required(VERSION 3.13)

# Firmware by default. -DPDG_HOST=ON builds the desktop simulator and the
# unit tests instead, with the host compiler; -DPDG_WEB=ON under emcmake
# builds the web version into web/.
option(PDG_HOST "Build the desktop simulator and tests instead of firmware" OFF)
option(PDG_WEB "Build the web version with Emscripten" OFF)

if(NOT PDG_HOST AND NOT PDG_WEB)
    if(NOT PICO_BOARD)
        set(PICO_BOARD pico)
    endif()
    include(cmake/pico_sdk_import.cmake)
endif()

project(pico_display_games C CXX ASM)
set(CMAKE_C_STANDARD 11)

# Bad Apple's frames, made by tools/encode_bad_apple.py. Without the file the
# app explains how to make it.
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
    src/core/app.c
    src/core/gfx.c
    src/core/save.c
    src/assets/assets.c
    ${APP_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/bad_apple_data.S
)

if(PDG_WEB)
    # The web build reads Bad Apple's frames from a file rather than linking them in.
    list(REMOVE_ITEM GAME_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/bad_apple_data.S)
    add_executable(pdg_web src/main.c ${GAME_SOURCES} src/platform/hal_sim.c)
    target_compile_definitions(pdg_web PRIVATE PDG_SIM PDG_SDL PDG_WEB)
    target_compile_options(pdg_web PRIVATE -sUSE_SDL=2 -Wall -Wextra -Wno-unused-parameter)
    target_link_options(pdg_web PRIVATE -sUSE_SDL=2 -sASYNCIFY -sALLOW_MEMORY_GROWTH=1
        -sEXPORTED_FUNCTIONS=_main,_web_button --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # Bad Apple's frames load beside the page as index.data, read on opening the app.
    if(EXISTS "${BAD_APPLE_BIN}")
        target_link_options(pdg_web PRIVATE --preload-file ${BAD_APPLE_BIN}@/bad_apple.bin)
    endif()
    set_target_properties(pdg_web PROPERTIES OUTPUT_NAME index SUFFIX ".html"
        LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # The page's fonts, served beside it.
    file(COPY assets/fonts/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/fonts)
elseif(NOT PDG_HOST)
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
