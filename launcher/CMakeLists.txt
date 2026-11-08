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
    game_process.cpp game_profile.cpp header_art.cpp incident_report.cpp log_scrubber.cpp options.cpp page_icons.cpp
    paths.cpp pkg_extract.cpp resolution.c settings.cpp trophy_content.cpp crypto_win.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_data.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trophy_key.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_pack.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trophy_runtime.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_service.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trophy_store.cpp
    ${PROJECT_SOURCE_DIR}/src/trophies/trophy_view.cpp ${PROJECT_SOURCE_DIR}/src/trophies/trp_archive.cpp)
target_include_directories(tsr_launcher_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(tsr_launcher_core PRIVATE TSR_PORT_VERSION="${PROJECT_VERSION}")
target_link_libraries(tsr_launcher_core PUBLIC trg_core SDL3::SDL3 tsr_keys tsr_startup_phase PRIVATE ZLIB::ZLIB bcrypt)
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

add_library(tsr_launcher STATIC host.cpp incident_page.cpp launcher.cpp package_browser.cpp problem_dialog.cpp shift_key.cpp
    trophy_page.cpp)
target_compile_definitions(tsr_launcher PRIVATE TSR_PORT_VERSION="${PROJECT_VERSION}")
target_include_directories(tsr_launcher PRIVATE ${PROJECT_SOURCE_DIR}/gpu/shim)
target_link_libraries(tsr_launcher PUBLIC tsr_launcher_core trg_launcher tsr_imgui_sdl ${CMAKE_DL_LIBS})

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
