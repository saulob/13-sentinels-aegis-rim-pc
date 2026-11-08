# The launcher: TRG Launcher (third_party/trg-launcher, fetched by tools\build.ps1) in a window of its own that is
# closed before the loader creates the game window.
set(TRG_DIR ${PROJECT_SOURCE_DIR}/third_party/trg-launcher)
if (NOT EXISTS ${TRG_DIR}/include/trg/launcher.h)
    message(FATAL_ERROR "TRG Launcher is missing: run tools\\build.ps1, which fetches it (gpu\\deps.txt).")
endif()
set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/gpu/third_party/imgui)

# TRG's settings file, platform helpers and HTTPS downloads do not need Dear ImGui.
add_library(trg_core STATIC ${TRG_DIR}/src/settings.cpp ${TRG_DIR}/src/platform.cpp ${TRG_DIR}/src/download.cpp)
target_include_directories(trg_core PUBLIC ${TRG_DIR}/include)
target_compile_definitions(trg_core PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(trg_core PRIVATE shell32 ole32 comdlg32 winhttp)

# The launcher draws with the Dear ImGui compiled into the video core library.
add_library(trg_launcher STATIC ${TRG_DIR}/src/launcher.cpp ${TRG_DIR}/src/headers.cpp)
target_include_directories(trg_launcher PUBLIC ${IMGUI_DIR})
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(trg_launcher PUBLIC trg_core tsrgpu)

# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_core trg_launcher PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_library(tsr_imgui_sdl STATIC ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(tsr_imgui_sdl PUBLIC ${IMGUI_DIR})
target_link_libraries(tsr_imgui_sdl PUBLIC SDL3::SDL3)

# Game files (check, PKG unpacking, install, boot image), the trophies (src/trophies), the settings file and the Display
# options: no window needed (tests link only this).
find_package(ZLIB REQUIRED)
add_library(tsr_launcher_core STATIC boot_image.cpp build_diagnostics.cpp controller_prompts.cpp cpk_patch.cpp language_support.cpp
    file_browser.cpp game_files.cpp game_install.cpp
    game_process.cpp game_profile.cpp header_art.cpp http_post.cpp incident_client.cpp incident_report.cpp json_lite.cpp
    log_scrubber.cpp options.cpp page_icons.cpp paths.cpp pkg_extract.cpp report_history.cpp resolution.c settings.cpp
    trophy_content.cpp crypto_win.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_data.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trophy_key.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_pack.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trophy_runtime.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_service.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trophy_store.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_view.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trp_archive.cpp)
target_include_directories(tsr_launcher_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(tsr_launcher_core PRIVATE TSR_PORT_VERSION="${PROJECT_VERSION}")
target_link_libraries(tsr_launcher_core PUBLIC trg_core SDL3::SDL3 tsr_keys tsr_startup_phase PRIVATE ZLIB::ZLIB bcrypt winhttp)
set(TSR_LOG_SOURCES ${PROJECT_SOURCE_DIR}/src/runtime_log.c ${PROJECT_SOURCE_DIR}/src/runtime_session_log.c)

# The sidebar's page icons (assets/launcher/icons/*.svg) as a table of byte arrays, page_icons.cpp.
file(GLOB TSR_PAGE_ICONS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/launcher/icons/*.svg)
list(SORT TSR_PAGE_ICONS)
set(TSR_PAGE_ICON_LIST ${CMAKE_BINARY_DIR}/generated/page_icons.txt)
file(CONFIGURE OUTPUT ${TSR_PAGE_ICON_LIST} CONTENT "${TSR_PAGE_ICONS}")
set(TSR_PAGE_ICON_SOURCE ${CMAKE_BINARY_DIR}/generated/page_icon_data.cpp)
add_custom_command(OUTPUT ${TSR_PAGE_ICON_SOURCE}
    COMMAND ${CMAKE_COMMAND} -DINPUT_LIST=${TSR_PAGE_ICON_LIST} -DOUTPUT=${TSR_PAGE_ICON_SOURCE} -DSYMBOL=kPageIconFiles
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_files.cmake
    DEPENDS ${TSR_PAGE_ICON_LIST} ${TSR_PAGE_ICONS} ${CMAKE_CURRENT_SOURCE_DIR}/embed_files.cmake
    COMMENT "Embedding the page icons")
target_sources(tsr_launcher_core PRIVATE ${TSR_PAGE_ICON_SOURCE})

# The Xbox controller icon patches (prompts/xbox-prompts-<version>.dcpx) as byte arrays, prompts_patch.h.
foreach(version 100 102)
    string(SUBSTRING ${version} 0 1 major)
    string(SUBSTRING ${version} 1 2 minor)
    set(patch ${CMAKE_CURRENT_SOURCE_DIR}/prompts/xbox-prompts-${major}.${minor}.dcpx)
    set(patch_source ${CMAKE_BINARY_DIR}/generated/xbox_prompts_${version}.cpp)
    add_custom_command(OUTPUT ${patch_source}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${patch} -DOUTPUT=${patch_source} -DSYMBOL=kXboxPrompts${version}Patch
                -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
        DEPENDS ${patch} ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
        COMMENT "Embedding prompts/xbox-prompts-${major}.${minor}.dcpx")
    target_sources(tsr_launcher_core PRIVATE ${patch_source})
endforeach()
target_include_directories(tsr_launcher_core PRIVATE ${PROJECT_SOURCE_DIR}/gpu/shim)

add_library(tsr_launcher STATIC host.cpp incident_page.cpp launcher.cpp my_reports_page.cpp package_browser.cpp problem_dialog.cpp
    shift_key.cpp trophy_page.cpp update_config.cpp
    updater/github_release.cpp updater/json.cpp updater/sha256.cpp updater/third_party/miniz/miniz.c
    updater/update_controller.cpp updater/update_job.cpp updater/update_paths.cpp updater/update_platform.cpp
    updater/update_ui.cpp updater/update_zip.cpp updater/version.cpp)
set_source_files_properties(updater/third_party/miniz/miniz.c PROPERTIES COMPILE_OPTIONS "-w")
target_compile_definitions(tsr_launcher PRIVATE TSR_PORT_VERSION="${PROJECT_VERSION}")
target_include_directories(tsr_launcher PRIVATE ${PROJECT_SOURCE_DIR}/gpu/shim)
target_link_libraries(tsr_launcher PUBLIC tsr_launcher_core trg_launcher tsr_imgui_sdl ${CMAKE_DL_LIBS})

# Windows helper that replaces the port's files after the launcher exits.
if (WIN32)
    add_executable(trg-updater WIN32 updater/helper_main.cpp updater/install_transaction.cpp updater/sha256.cpp
        updater/update_job.cpp updater/update_paths.cpp updater/update_platform.cpp)
    target_include_directories(trg-updater PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(trg-updater PRIVATE shell32)
    target_link_options(trg-updater PRIVATE -static)
    set_target_properties(trg-updater PROPERTIES OUTPUT_NAME "trg-updater" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_compile_options(trg-updater PRIVATE -Wall -Wextra)

    add_executable(tsr-update-restart-marker EXCLUDE_FROM_ALL WIN32 ../tests/update_restart_marker.cpp)
    target_link_libraries(tsr-update-restart-marker PRIVATE shell32)
    target_link_options(tsr-update-restart-marker PRIVATE -static)
    set_target_properties(tsr-update-restart-marker PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

    add_executable(tsr-update-helper-tests EXCLUDE_FROM_ALL ../tests/update_helper_tests.cpp)
    target_compile_definitions(tsr-update-helper-tests PRIVATE
        TSR_UPDATER_EXE="$<TARGET_FILE:trg-updater>"
        TSR_MARKER_EXE="$<TARGET_FILE:tsr-update-restart-marker>")
    add_dependencies(tsr-update-helper-tests trg-updater tsr-update-restart-marker)
    set_target_properties(tsr-update-helper-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_compile_options(tsr-update-helper-tests PRIVATE -Wall -Wextra)
endif()

# Focused updater tests: versions, release JSON, SHA-256, ZIP safety, staging, jobs, rollback and the 13S release
# contract; no network.
add_executable(tsr-update-tests EXCLUDE_FROM_ALL ../tests/update_tests.cpp update_config.cpp
    updater/github_release.cpp updater/install_transaction.cpp updater/json.cpp updater/sha256.cpp
    updater/update_job.cpp updater/update_paths.cpp updater/update_platform.cpp updater/update_zip.cpp
    updater/version.cpp updater/third_party/miniz/miniz.c)
target_include_directories(tsr-update-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(tsr-update-tests PRIVATE TSR_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    TSR_UPDATER_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}" TSR_PORT_VERSION="${PROJECT_VERSION}")
target_link_libraries(tsr-update-tests PRIVATE trg_core shell32)
set_target_properties(tsr-update-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(tsr-update-tests PRIVATE -Wall -Wextra)

# Update failures against a scripted loopback HTTP server: 504, dropped connections, stalls, redirects and retries.
add_executable(tsr-update-network-tests EXCLUDE_FROM_ALL ../tests/update_network_tests.cpp
    updater/github_release.cpp updater/install_transaction.cpp updater/json.cpp updater/sha256.cpp
    updater/update_controller.cpp updater/update_job.cpp updater/update_paths.cpp updater/update_platform.cpp
    updater/update_zip.cpp updater/version.cpp updater/third_party/miniz/miniz.c)
target_include_directories(tsr-update-network-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tsr-update-network-tests PRIVATE trg_core shell32 ws2_32)
set_target_properties(tsr-update-network-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(tsr-update-network-tests PRIVATE -Wall -Wextra)

set_target_properties(tsr_launcher_core tsr_launcher PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(tsr_launcher_core PRIVATE -Wall -Wextra)
target_compile_options(tsr_launcher PRIVATE -Wall -Wextra)

# Focused tests with synthetic data and temporary folders (tools\test.ps1); no window, no network, not part of the
# default build. TSR_TEST_PACKAGES names a folder with the real packages, which are only opened.
add_executable(tsr-game-profile-tests EXCLUDE_FROM_ALL ../tests/game_profile_tests.cpp)
target_compile_definitions(tsr-game-profile-tests PRIVATE TSR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(tsr-game-profile-tests PRIVATE tsr_launcher_core)
set_target_properties(tsr-game-profile-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(tsr-game-profile-tests PRIVATE -Wall -Wextra)

add_executable(tsr-launcher-tests EXCLUDE_FROM_ALL ../tests/launcher_tests.cpp)
target_compile_definitions(tsr-launcher-tests PRIVATE TSR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(tsr-launcher-tests PRIVATE tsr_launcher_core)
set_target_properties(tsr-launcher-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(tsr-launcher-tests PRIVATE -Wall -Wextra)

# The installed game's archives are only read: the patch is applied to a copy in the temporary folder.
add_executable(tsr-game-feature-tests EXCLUDE_FROM_ALL ../tests/game_feature_tests.cpp)
target_compile_definitions(tsr-game-feature-tests PRIVATE TSR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(tsr-game-feature-tests PRIVATE tsr_launcher_core)
set_target_properties(tsr-game-feature-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(tsr-game-feature-tests PRIVATE -Wall -Wextra)

# The Bug Report page's logic against a stand-in transport and synthetic logs: no window, no network.
add_executable(tsr-bug-report-tests EXCLUDE_FROM_ALL ../tests/bug_report_tests.cpp)
target_compile_definitions(tsr-bug-report-tests PRIVATE TSR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(tsr-bug-report-tests PRIVATE tsr_launcher_core)
set_target_properties(tsr-bug-report-tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(tsr-bug-report-tests PRIVATE -Wall -Wextra)
