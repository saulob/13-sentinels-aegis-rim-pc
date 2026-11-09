cmake_minimum_required(VERSION 3.24)
project(thirteen_sentinels_aegis_rim_pc VERSION 0.1.0 LANGUAGES C CXX)
set(TSR_PORT_VERSION "${PROJECT_VERSION}")

if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Build with Clang: MSYS2 CLANG64 (tools\\build.ps1) on Windows, tools/build.sh on Linux.")
endif()
if (NOT WIN32 AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$"))
    message(FATAL_ERROR "Supported targets: Windows x64 and Linux x86_64.")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if (WIN32)
    set(CMAKE_C_EXTENSIONS OFF)
else()
    set(CMAKE_C_EXTENSIONS ON) # POSIX interfaces (sigjmp_buf, memfd_create) are hidden in strict ISO mode
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# Repository paths in __FILE__ and std::source_location stay out of the binary; debug info keeps them.
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=>")
if (NOT WIN32)
    # Non-PIE: the heap handed to the guest (thread handles, TLS) must stay below 1 TiB (loader.c).
    set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fno-pie>")
    add_link_options(-no-pie)
endif()

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
target_link_libraries(tsr_atrac9 PUBLIC $<$<NOT:$<BOOL:${WIN32}>>:m>)
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

if (WIN32)
    enable_language(RC)
    # Relink when the application icon, the manifest or the version changes.
    set_source_files_properties(src/13_sentinels_aegis_rim.rc PROPERTIES OBJECT_DEPENDS
        "${CMAKE_SOURCE_DIR}/packaging/icons/app-icon.ico;${CMAKE_SOURCE_DIR}/src/13_sentinels_aegis_rim.manifest;${CMAKE_BINARY_DIR}/generated/tsr_version.h")
    set(TSR_PLATFORM_SOURCES src/compat_win.c src/module_diagnostics.c src/module_history.c src/call_site.c src/runtime_log.c
        src/13_sentinels_aegis_rim.rc)
else()
    set(TSR_PLATFORM_SOURCES src/runtime_log_posix.c)
endif()

set(TSR_HOOK_SOURCES src/guest_hook.c src/code_signature.c src/skip_intro.c src/exit_game.c src/exit_game_assets.c
    src/exit_game_data.c)
# The Exit Game title labels are assembled into exit_game_data.c (.incbin): rebuild it when they change.
set(TSR_EXIT_GAME_LABELS ${CMAKE_SOURCE_DIR}/assets/exit_game/title-labels.bin)
set_source_files_properties(src/exit_game_data.c PROPERTIES
    COMPILE_DEFINITIONS "TSR_EXIT_GAME_LABELS_PATH=\"${TSR_EXIT_GAME_LABELS}\""
    OBJECT_DEPENDS "${TSR_EXIT_GAME_LABELS}")
set(TSR_GAME_SOURCES src/loader.c src/isa_emulate.c src/lzcnt_compat.c ${TSR_PLATFORM_SOURCES} ${TSR_RUNTIME_SOURCES}
    ${TSR_HOOK_SOURCES})
if (WIN32)
    # One program for the launcher and the game, Windows subsystem: no console window. The run log takes stdout and
    # stderr (src/runtime_log.c).
    add_executable(tsr_game WIN32 ${TSR_GAME_SOURCES})
else()
    add_executable(tsr_game ${TSR_GAME_SOURCES})
endif()
set_target_properties(tsr_game PROPERTIES OUTPUT_NAME "13_sentinels_aegis_rim")
target_include_directories(tsr_game PRIVATE src launcher gpu/shim ${CMAKE_BINARY_DIR}/generated)
target_compile_options(tsr_game PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror -fno-omit-frame-pointer>)
if (WIN32)
    # Windows 10 1903+ APIs (VirtualAlloc2, MapViewOfFile3).
    target_compile_definitions(tsr_game PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000007)
    # The PS4 user range (64 GiB - 1 TiB) must stay free of host allocations: no high-entropy ASLR.
    # The guest main thread runs on the process stack: 16 MiB, committed (guest code has no stack probes).
    target_link_options(tsr_game PRIVATE -Wl,--disable-high-entropy-va "SHELL:-Xlinker --stack -Xlinker 16777216,16777216")
    target_link_libraries(tsr_game PRIVATE tsr_launcher tsrgpu tsr_atrac9 SDL3::SDL3 Freetype::Freetype onecore bcrypt ws2_32 -pthread)
    # A release folder holds the update helper next to the program (launcher/CMakeLists.txt).
    add_dependencies(tsr_game trg-updater)
else()
    # -rdynamic: fault reports name host symbols (dladdr).
    target_link_options(tsr_game PRIVATE -rdynamic)
    # The pinned FFmpeg (build/deps) is found relative to the executable, so no path of this machine is written into
    # it; the AppImage sets its own RPATH.
    list(GET FFMPEG_LIBRARY_DIRS 0 TSR_FFMPEG_LIBRARY_DIR)
    string(FIND "${TSR_FFMPEG_LIBRARY_DIR}/" "${CMAKE_SOURCE_DIR}/" TSR_FFMPEG_IN_TREE)
    if (TSR_FFMPEG_IN_TREE EQUAL 0)
        file(RELATIVE_PATH TSR_FFMPEG_RPATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" "${TSR_FFMPEG_LIBRARY_DIR}")
        set_target_properties(tsr_game PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "\$ORIGIN/${TSR_FFMPEG_RPATH}")
    endif()
    target_link_libraries(tsr_game PRIVATE tsr_launcher tsrgpu tsr_atrac9 SDL3::SDL3 Freetype::Freetype Threads::Threads
        ${CMAKE_DL_LIBS} m)
endif()

# The launcher reads its images next to the executable; each copy depends on its source file, so an image-only change syncs.
set(TSR_LAUNCHER_ASSET_DIR "$<TARGET_FILE_DIR:tsr_game>/assets/launcher")
set(TSR_LAUNCHER_ASSET_COPIES)
foreach(asset header-art.png sponsor-heart.png)
    set(asset_source "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/${asset}")
    set(asset_stamp "${CMAKE_CURRENT_BINARY_DIR}/launcher-assets/${asset}.stamp")
    add_custom_command(OUTPUT "${asset_stamp}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TSR_LAUNCHER_ASSET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${asset_source}" "${TSR_LAUNCHER_ASSET_DIR}/${asset}"
        COMMAND ${CMAKE_COMMAND} -E touch "${asset_stamp}"
        DEPENDS "${asset_source}"
        VERBATIM)
    list(APPEND TSR_LAUNCHER_ASSET_COPIES "${asset_stamp}")
endforeach()
add_custom_target(tsr_launcher_assets DEPENDS ${TSR_LAUNCHER_ASSET_COPIES})
add_dependencies(tsr_game tsr_launcher_assets)

# Focused headless tests (tools\test.ps1); none is part of the default build and none opens a window or starts the game.
function(tsr_add_test name)
    add_executable(${name} EXCLUDE_FROM_ALL ${ARGN})
    set_target_properties(${name} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_include_directories(${name} PRIVATE src gpu/shim)
    target_compile_definitions(${name} PRIVATE TSR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_options(${name} PRIVATE -Wall -Wextra)
endfunction()

if (WIN32)
    tsr_add_test(tsr-memory-windows-tests tests/memory_windows_tests.c)
    target_compile_definitions(tsr-memory-windows-tests PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000007)
    target_link_libraries(tsr-memory-windows-tests PRIVATE onecore -pthread)
    target_link_options(tsr-memory-windows-tests PRIVATE -Wl,--disable-high-entropy-va)
    tsr_add_test(tsr-log-tests tests/log_tests.c src/runtime_log.c src/runtime_session_log.c)
    set(TSR_TEST_SYSTEM_LIBS bcrypt -pthread)
    set(TSR_TEST_COMPAT_SOURCES src/compat_win.c)
else()
    set(TSR_TEST_SYSTEM_LIBS Threads::Threads ${CMAKE_DL_LIBS} m)
    set(TSR_TEST_COMPAT_SOURCES)
endif()
# Linux guest thread stacks come from the guest memory layer (below 1 TiB).
set(TSR_TEST_STACK_SOURCES $<$<NOT:$<BOOL:${WIN32}>>:src/runtime_memory.c>)
tsr_add_test(tsr-game-hook-tests tests/game_hook_tests.c ${TSR_HOOK_SOURCES})
tsr_add_test(tsr-exit-game-tests tests/exit_game_tests.c ${TSR_HOOK_SOURCES})
tsr_add_test(tsr-isa-tests tests/isa_tests.c src/isa_emulate.c src/lzcnt_compat.c)
target_compile_options(tsr-isa-tests PRIVATE -Werror)
tsr_add_test(tsr-module-history-tests tests/module_history_tests.c src/module_history.c src/call_site.c src/startup_phase.c)
tsr_add_test(tsr-sdl-compat-tests tests/sdl_compat_tests.cpp)
tsr_add_test(tsr-window-tests tests/window_fit_tests.cpp)
tsr_add_test(tsr-frame-pacing-tests tests/frame_pacing_tests.cpp)
tsr_add_test(tsr-runtime-tests tests/runtime_tests.c src/runtime_mutex.c ${TSR_TEST_COMPAT_SOURCES} ${TSR_TEST_STACK_SOURCES})
target_link_libraries(tsr-runtime-tests PRIVATE ${TSR_TEST_SYSTEM_LIBS})
tsr_add_test(tsr-savedata-tests tests/savedata_tests.c src/runtime_file.c ${TSR_TEST_COMPAT_SOURCES})
target_compile_definitions(tsr-savedata-tests PRIVATE TSR_SAVEDATA_TEST_ROOT="${CMAKE_BINARY_DIR}/savedata-tests")
target_link_libraries(tsr-savedata-tests PRIVATE ${TSR_TEST_SYSTEM_LIBS})
tsr_add_test(tsr-ajm-tests tests/ajm_tests.c)
target_link_libraries(tsr-ajm-tests PRIVATE tsr_atrac9 -pthread)
tsr_add_test(tsr-graphics-tests tests/graphics_tests.cpp)
target_link_libraries(tsr-graphics-tests PRIVATE tsr_launcher_core)
tsr_add_test(tsr-build-parity-tests tests/build_parity_tests.cpp ${TSR_HOOK_SOURCES} src/lzcnt_compat.c src/isa_emulate.c)
target_link_libraries(tsr-build-parity-tests PRIVATE tsr_launcher_core)
# Windows and Linux offer the same features: the shared feature contract of the nine builds and the platform matrix.
tsr_add_test(tsr-platform-parity-tests tests/platform_parity_tests.cpp ${TSR_HOOK_SOURCES} src/lzcnt_compat.c src/isa_emulate.c
    launcher/update_config.cpp launcher/updater/update_platform.cpp launcher/updater/update_paths.cpp
    launcher/updater/sha256.cpp launcher/updater/version.cpp)
target_include_directories(tsr-platform-parity-tests PRIVATE launcher ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(tsr-platform-parity-tests PRIVATE TSR_PORT_VERSION="${PROJECT_VERSION}"
    TSR_PLATFORM="$<IF:$<BOOL:${WIN32}>,windows,linux>")
target_link_libraries(tsr-platform-parity-tests PRIVATE tsr_launcher_core $<$<BOOL:${WIN32}>:shell32>)
tsr_add_test(tsr-command-snapshot-tests tests/command_snapshot_tests.cpp)
target_include_directories(tsr-command-snapshot-tests PRIVATE gpu/shadps4)
target_link_libraries(tsr-command-snapshot-tests PRIVATE Threads::Threads)
tsr_add_test(tsr-renderer-selection-tests tests/renderer_selection_tests.cpp)
set_target_properties(tsr-renderer-selection-tests PROPERTIES CXX_STANDARD 23)
target_include_directories(tsr-renderer-selection-tests PRIVATE gpu/shadps4)
# The OpenGL fast paths against real page faults and recording entry points; no context.
tsr_add_test(tsr-opengl-perf-tests tests/opengl_perf_tests.cpp)
set_target_properties(tsr-opengl-perf-tests PROPERTIES CXX_STANDARD 23)
target_include_directories(tsr-opengl-perf-tests PRIVATE gpu/shadps4)
target_link_libraries(tsr-opengl-perf-tests PRIVATE SDL3::SDL3 Threads::Threads)
# The detiler and the emitted GLSL through glslangValidator; links the video core, no context.
tsr_add_test(tsr-opengl-backend-tests tests/opengl_backend_tests.cpp)
set_target_properties(tsr-opengl-backend-tests PROPERTIES CXX_STANDARD 23)
target_include_directories(tsr-opengl-backend-tests PRIVATE gpu/shadps4 tests ${CMAKE_BINARY_DIR}/gpu/include
    gpu/third_party/gcn/include gpu/third_party/half/include gpu/third_party/xbyak)
target_compile_definitions(tsr-opengl-backend-tests PRIVATE TSR_GLSLANG="${GLSLANG_COMPILER}")
target_link_libraries(tsr-opengl-backend-tests PRIVATE tsrgpu $<$<BOOL:${WIN32}>:onecore>)
