cmake_minimum_required(VERSION 3.24)
project(thirteen_sentinels_aegis_rim_pc VERSION 0.1.0 LANGUAGES C CXX)
set(TSR_PORT_VERSION "${PROJECT_VERSION}")

if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Build with Clang: MSYS2 CLANG64 (tools\\build.ps1).")
endif()
if (NOT WIN32)
    message(FATAL_ERROR "Supported target: Windows x64.")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# Repository paths in __FILE__ and std::source_location stay out of the binary; debug info keeps them.
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=>")

if (NOT PROJECT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR "project VERSION must start with MAJOR.MINOR.PATCH")
endif()
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/tsr_version.h CONTENT "#define TSR_VERSION_NUMBERS \
${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0\n#define TSR_FILE_VERSION_STRING \
\"${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.0\"\n#define TSR_VERSION_STRING \"${PROJECT_VERSION}\"\n")

find_package(SDL3 CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(Threads REQUIRED)

# Keyboard bindings shared by the launcher, the runtime pad and the video core.
add_library(tsr_keys STATIC src/tsr_keys.c)
target_include_directories(tsr_keys PUBLIC src)
target_link_libraries(tsr_keys PUBLIC SDL3::SDL3)
target_compile_options(tsr_keys PRIVATE -Wall -Wextra -Werror)

# The stage the process is in, for the crash report: used by the launcher, the loader and the window thread.
add_library(tsr_startup_phase STATIC src/startup_phase.c)
target_include_directories(tsr_startup_phase PUBLIC src)
target_compile_options(tsr_startup_phase PRIVATE -Wall -Wextra -Werror)

# The launcher's and the in-game menus' text lookup (header only).
add_library(tsr_ui_text INTERFACE)
target_include_directories(tsr_ui_text INTERFACE launcher)

add_subdirectory(gpu)
add_subdirectory(launcher)

# ATRAC9 decoding for libSceAjm (third_party/LibAtrac9, fetched by tools\build.ps1).
set(TSR_ATRAC9_DIR ${CMAKE_SOURCE_DIR}/third_party/LibAtrac9/C/src)
if (NOT EXISTS ${TSR_ATRAC9_DIR}/libatrac9.h)
    message(FATAL_ERROR "LibAtrac9 is missing: run tools\\build.ps1, which fetches it (gpu\\deps.txt).")
endif()
file(GLOB TSR_ATRAC9_SOURCES CONFIGURE_DEPENDS ${TSR_ATRAC9_DIR}/*.c)
add_library(tsr_atrac9 STATIC ${TSR_ATRAC9_SOURCES})
target_include_directories(tsr_atrac9 PUBLIC ${TSR_ATRAC9_DIR})
target_compile_options(tsr_atrac9 PRIVATE -w)
set_target_properties(tsr_atrac9 PROPERTIES C_EXTENSIONS ON)

set(TSR_RUNTIME_SOURCES
    src/runtime.c
    src/runtime_ajm.c
    src/runtime_audio.c
    src/runtime_content.c
    src/runtime_eventflag.c
    src/runtime_extra.c
    src/runtime_fiber.c
    src/runtime_file.c
    src/runtime_font.c
    src/runtime_kernel.c
    src/runtime_memory.c
    src/runtime_mutex.c
    src/runtime_pad.c
    src/runtime_redzone.c
    src/runtime_rtc.c
    src/runtime_rwlock.c
    src/runtime_savedata.c
    src/runtime_sema.c
    src/runtime_services.c
    src/runtime_session_log.c
    src/runtime_thread.c
    src/runtime_time.c
    src/runtime_tsr.c)
# The built-in system fonts are assembled into runtime_font.c (.incbin): rebuild it when a font changes.
set(TSR_NOTO_SANS ${CMAKE_SOURCE_DIR}/third_party/fonts/NotoSans-Regular.ttf)
set(TSR_NOTO_SANS_TC ${CMAKE_SOURCE_DIR}/third_party/fonts/NotoSansTC-Regular.otf)
set_source_files_properties(src/runtime_font.c PROPERTIES
    COMPILE_DEFINITIONS "TSR_NOTO_SANS_PATH=\"${TSR_NOTO_SANS}\";TSR_NOTO_SANS_TC_PATH=\"${TSR_NOTO_SANS_TC}\""
    OBJECT_DEPENDS "${TSR_NOTO_SANS};${TSR_NOTO_SANS_TC}")

enable_language(RC)
# Relink when the application icon, the manifest or the version changes.
set_source_files_properties(src/13_sentinels_aegis_rim.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/packaging/icons/app-icon.ico;${CMAKE_SOURCE_DIR}/src/13_sentinels_aegis_rim.manifest;${CMAKE_BINARY_DIR}/generated/tsr_version.h")

set(TSR_HOOK_SOURCES src/guest_hook.c src/code_signature.c src/skip_intro.c)
add_executable(tsr_game WIN32 src/loader.c src/isa_emulate.c src/compat_win.c src/module_diagnostics.c src/module_history.c
    src/call_site.c src/runtime_log.c src/13_sentinels_aegis_rim.rc ${TSR_RUNTIME_SOURCES} ${TSR_HOOK_SOURCES})
set_target_properties(tsr_game PROPERTIES OUTPUT_NAME "13_sentinels_aegis_rim")
target_include_directories(tsr_game PRIVATE src launcher gpu/shim ${CMAKE_BINARY_DIR}/generated)
target_compile_options(tsr_game PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror -fno-omit-frame-pointer>)
# Windows 10 1903+ APIs (VirtualAlloc2, MapViewOfFile3).
target_compile_definitions(tsr_game PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000007)
# The PS4 user range (64 GiB - 1 TiB) must stay free of host allocations: no high-entropy ASLR.
# The guest main thread runs on the process stack: 16 MiB, committed (guest code has no stack probes).
target_link_options(tsr_game PRIVATE -Wl,--disable-high-entropy-va "SHELL:-Xlinker --stack -Xlinker 16777216,16777216")
target_link_libraries(tsr_game PRIVATE tsr_launcher tsrgpu tsr_atrac9 SDL3::SDL3 Freetype::Freetype onecore bcrypt ws2_32 -pthread)

# The launcher's own entry point: opens the launcher of the executable beside it.
add_executable(tsr_launcher_entry WIN32 src/launcher_entry.c src/13_sentinels_launcher.rc)
set_source_files_properties(src/13_sentinels_launcher.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/packaging/icons/app-icon.ico;${CMAKE_BINARY_DIR}/generated/tsr_version.h")
set_target_properties(tsr_launcher_entry PROPERTIES OUTPUT_NAME "13_sentinels_launcher")
target_include_directories(tsr_launcher_entry PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_compile_options(tsr_launcher_entry PRIVATE -Wall -Wextra -Werror)
target_link_options(tsr_launcher_entry PRIVATE -static)
add_dependencies(tsr_game tsr_launcher_entry)

# The launcher reads its images next to the executable.
add_custom_command(TARGET tsr_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tsr_game>/assets/launcher"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/header-art.png"
        "$<TARGET_FILE_DIR:tsr_game>/assets/launcher/header-art.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/sponsor-heart.png"
        "$<TARGET_FILE_DIR:tsr_game>/assets/launcher/sponsor-heart.png"
    VERBATIM)

# Focused headless tests (tools\test.ps1); none is part of the default build and none opens a window or starts the game.
function(tsr_add_test name)
    add_executable(${name} EXCLUDE_FROM_ALL ${ARGN})
    set_target_properties(${name} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_include_directories(${name} PRIVATE src gpu/shim)
    target_compile_definitions(${name} PRIVATE TSR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_options(${name} PRIVATE -Wall -Wextra)
endfunction()

tsr_add_test(tsr-memory-windows-tests tests/memory_windows_tests.c)
target_compile_definitions(tsr-memory-windows-tests PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000007)
target_link_libraries(tsr-memory-windows-tests PRIVATE onecore -pthread)
target_link_options(tsr-memory-windows-tests PRIVATE -Wl,--disable-high-entropy-va)
tsr_add_test(tsr-log-tests tests/log_tests.c src/runtime_log.c src/runtime_session_log.c)
tsr_add_test(tsr-game-hook-tests tests/game_hook_tests.c ${TSR_HOOK_SOURCES})
tsr_add_test(tsr-module-history-tests tests/module_history_tests.c src/module_history.c src/call_site.c src/startup_phase.c)
tsr_add_test(tsr-sdl-compat-tests tests/sdl_compat_tests.cpp)
tsr_add_test(tsr-window-tests tests/window_fit_tests.cpp)
tsr_add_test(tsr-frame-pacing-tests tests/frame_pacing_tests.cpp)
tsr_add_test(tsr-runtime-tests tests/runtime_tests.c src/runtime_mutex.c src/compat_win.c)
target_link_libraries(tsr-runtime-tests PRIVATE bcrypt -pthread)
tsr_add_test(tsr-savedata-tests tests/savedata_tests.c src/runtime_file.c src/compat_win.c)
target_compile_definitions(tsr-savedata-tests PRIVATE TSR_SAVEDATA_TEST_ROOT="${CMAKE_BINARY_DIR}/savedata-tests")
target_link_libraries(tsr-savedata-tests PRIVATE bcrypt -pthread)
tsr_add_test(tsr-ajm-tests tests/ajm_tests.c)
target_link_libraries(tsr-ajm-tests PRIVATE tsr_atrac9 -pthread)
