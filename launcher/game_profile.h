// The supported game builds. scripts/game_profiles.py mirrors the identity fields; tests/game_profile_tests.cpp checks they agree.
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "tsr_resolution.h"

namespace tsr {

inline constexpr char kGameTitle[] = "13 Sentinels: Aegis Rim";
inline constexpr char kPortRepository[] = "github.com/saulob/13-sentinels-aegis-rim-pc";
inline constexpr char kIssuesUrl[] = "https://github.com/saulob/13-sentinels-aegis-rim-pc/issues";
inline constexpr char kAuthorUrl[] = "https://github.com/saulob";
inline constexpr char kSponsorUrl[] = "https://github.com/sponsors/saulob";

// A package of a build, by its param.sfo APP_VER and VERSION. An update (CATEGORY gp) is applied over the packages
// before it.
struct PackageLayer {
    const char* app_ver;
    const char* sfo_version;
    bool update;
};

// A system language the game can be told for its menus and subtitles.
struct TextLanguage {
    std::string_view key;
    std::string_view name;
    int ps4_id;
    std::string_view archive; // holds the language's text; the language is offered only when it is installed
};

struct GameBuild {
    const char* title_id;
    const char* content_id;
    const char* version;          // the profile version: the preparation marker's "version"
    const char* folder;           // under game\prepared and game\image; distinct for each build
    const char* eboot_sha256;     // lower-case hex of the finished game's eboot.bin
    const char* regional_archive; // the build's main archive
    std::span<const char* const> extra_files; // the other archives and files the build's executable needs
    const char* label;            // the name the log, the launcher and the loader show
    const char* region;
    std::span<const PackageLayer> layers; // base package first; the last layer is the finished game
    std::span<const TsrResolutionSite> resolution_sites; // empty: runs only at its native size
    std::span<const TextLanguage> text_languages;        // the first is the default
    const char* support_note;     // what is known about running this build, shown on the Play page
};

// The supported builds: the base first, then its updates.
std::span<const GameBuild> SupportedBuilds();

// The finished game's layer: its param.sfo is the one of the installed game.
const PackageLayer& FinishedLayer(const GameBuild& build);

// The finished package's APP_VER and VERSION, formatted for display (01.00 becomes 1.00).
std::string BuildMetadataText(const GameBuild& build);

// True for a build finished by an update package.
inline bool IsUpdateBuild(const GameBuild& build) {
    return build.layers.size() > 1;
}

// The build whose base package a build's update is laid over, or nullptr when the build has no update.
const GameBuild* BaseBuildOf(const GameBuild& build, std::span<const GameBuild> builds = SupportedBuilds());

// True when the build's executable has no verified render resolution sites: it then runs only at its native size.
// The launcher hides and the loader refuses the custom Render resolution and Aspect Ratio for such a build; the unknown
// build (nullptr) is not restricted here, and the loader refuses to patch it anyway.
bool RestrictedToNativeResolution(const GameBuild* build);

// The build's languages; the first supported build's when the build is not known.
std::span<const TextLanguage> TextLanguagesOf(const GameBuild* build);

// The language of a saved key among `languages` (not empty); the first when the key is not one of them.
const TextLanguage& ResolveTextLanguage(std::span<const TextLanguage> languages, std::string_view key);

std::string SupportedBuildsText();

// Files every complete game folder has, whatever the build. The build's archives are required too.
inline constexpr const char* kSharedGameFiles[] = {
    "eboot.bin",
    "sce_discmap.plt",
    "sce_module/libc.prx",
    "sce_module/libSceFios2.prx",
    "sce_module/libSceNpToolkit2.prx",
    "sce_sys/param.sfo",
};

// Linked after the eboot in this order: libc.prx needs libSceFios2.prx.
inline constexpr const char* kBundledModules[] = {"libc.prx", "libSceFios2.prx", "libSceNpToolkit2.prx"};

// Written last into a prepared game folder (scripts/game_view.py writes the same one).
inline constexpr char kPreparedMarker[] = ".tsr_view.json";

} // namespace tsr
