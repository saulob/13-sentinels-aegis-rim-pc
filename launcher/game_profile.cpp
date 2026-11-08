#include "game_profile.h"

#include <string_view>

namespace tsr {
namespace {

constexpr PackageLayer kBaseLayers[] = {{"01.00", "01.00", false}};
constexpr PackageLayer kUpdate101Layers[] = {{"01.00", "01.00", false}, {"01.01", "01.00", true}};
constexpr PackageLayer kUpdate102Layers[] = {{"01.00", "01.00", false}, {"01.02", "01.00", true}};

constexpr const char* kBaseFiles[] = {"ROBO_FR.CPK", "ROBO_GE.CPK", "ROBO_IT.CPK", "ROBO_SP.CPK"};
// Each update is laid over the base alone. 1.01 opens ROBO_PATCH_US.CPK, 1.02 also ROBO_PATCH2%s.CPK.
constexpr const char* kUpdate101Files[] = {"ROBO_FR.CPK", "ROBO_GE.CPK", "ROBO_IT.CPK", "ROBO_SP.CPK",
                                           "ROBO_PATCH_US.CPK", "sce_discmap_patch.plt"};
constexpr const char* kUpdate102Files[] = {
    "ROBO_FR.CPK",         "ROBO_GE.CPK",        "ROBO_IT.CPK",        "ROBO_SP.CPK",
    "ROBO_PATCH_US.CPK",   "ROBO_PATCH2_US.CPK", "ROBO_PATCH2_FR.CPK", "ROBO_PATCH2_GE.CPK",
    "ROBO_PATCH2_IT.CPK",  "ROBO_PATCH2_SP.CPK", "sce_discmap_patch.plt",
};

constexpr const char* kBasePromptArchives[] = {"ROBO_US.CPK", "ROBO_FR.CPK", "ROBO_GE.CPK", "ROBO_IT.CPK", "ROBO_SP.CPK"};
// 1.02 reads its button atlas from the ROBO_PATCH2 archives and the manual pages from the base ones.
constexpr const char* kUpdatePromptArchives[] = {"ROBO_US.CPK",        "ROBO_FR.CPK",        "ROBO_GE.CPK",
                                                 "ROBO_IT.CPK",        "ROBO_SP.CPK",        "ROBO_PATCH2_US.CPK",
                                                 "ROBO_PATCH2_FR.CPK", "ROBO_PATCH2_GE.CPK", "ROBO_PATCH2_IT.CPK",
                                                 "ROBO_PATCH2_SP.CPK"};

constexpr TextLanguage kLanguages[] = {
    {"english", "English", 1, "ROBO_US.CPK"}, {"french", "French", 2, "ROBO_FR.CPK"}, {"german", "German", 4, "ROBO_GE.CPK"},
    {"italian", "Italian", 5, "ROBO_IT.CPK"}, {"spanish", "Spanish", 3, "ROBO_SP.CPK"},
};

constexpr char kUnverifiedNote[] = "Not yet verified to run on this PC port.";
constexpr char kConfirmedNote[] = "";

constexpr GameBuild kGameBuilds[] = {
    {
        "CUSA19610",
        "UP0177-CUSA19610_00-13SENTINELSEFIGS",
        "1.00",
        "CUSA19610-1.00",
        "cf7e90939842b3c9c02f9116e26c2e398610312217fea05d52a1595c60ed5950",
        "ROBO_US.CPK",
        kBaseFiles,
        "CUSA19610 1.00 USA",
        "USA",
        kBaseLayers,
        {},
        kLanguages,
        kConfirmedNote,
        kBasePromptArchives,
        false,
    },
    {
        "CUSA19610",
        "UP0177-CUSA19610_00-13SENTINELSEFIGS",
        "1.01",
        "CUSA19610-1.01",
        "dafcc5f005da0eb74f7289c6dad4e8b8079a400e4832b3bc0a47b1c26a050533",
        "ROBO_US.CPK",
        kUpdate101Files,
        "CUSA19610 1.01 USA",
        "USA",
        kUpdate101Layers,
        {},
        kLanguages,
        kUnverifiedNote,
        kBasePromptArchives,
        true,
    },
    {
        "CUSA19610",
        "UP0177-CUSA19610_00-13SENTINELSEFIGS",
        "1.02",
        "CUSA19610-1.02",
        "075250099ba56c2e8f35b4dc8d35b25c5f98304e45c8546c790d56fe87803ff1",
        "ROBO_US.CPK",
        kUpdate102Files,
        "CUSA19610 1.02 USA",
        "USA",
        kUpdate102Layers,
        {},
        kLanguages,
        kConfirmedNote,
        kUpdatePromptArchives,
        true,
    },
};

bool SameLayer(const PackageLayer& a, const PackageLayer& b) {
    return std::string_view(a.app_ver) == b.app_ver && std::string_view(a.sfo_version) == b.sfo_version && a.update == b.update;
}

std::string DisplayVersion(std::string_view version) {
    return std::string(version.size() > 1 && version.front() == '0' ? version.substr(1) : version);
}

} // namespace

std::span<const GameBuild> SupportedBuilds() {
    return kGameBuilds;
}

const PackageLayer& FinishedLayer(const GameBuild& build) {
    return build.layers.back();
}

std::string BuildMetadataText(const GameBuild& build) {
    const PackageLayer& finished = FinishedLayer(build);
    return "APP_VER " + DisplayVersion(finished.app_ver) + " \xC2\xB7 VERSION " + DisplayVersion(finished.sfo_version);
}

const GameBuild* BaseBuildOf(const GameBuild& build, std::span<const GameBuild> builds) {
    if (!IsUpdateBuild(build)) {
        return nullptr;
    }
    for (const GameBuild& candidate : builds) {
        if (std::string_view(candidate.title_id) == build.title_id && candidate.layers.size() == 1 &&
            SameLayer(candidate.layers.front(), build.layers.front())) {
            return &candidate;
        }
    }
    return nullptr;
}

std::span<const TextLanguage> TextLanguagesOf(const GameBuild* build) {
    return build ? build->text_languages : std::span<const TextLanguage>(kLanguages);
}

bool RestrictedToNativeResolution(const GameBuild* build) {
    return build && build->resolution_sites.empty();
}

const TextLanguage& ResolveTextLanguage(std::span<const TextLanguage> languages, std::string_view key) {
    for (const TextLanguage& language : languages) {
        if (language.key == key) {
            return language;
        }
    }
    return languages.front();
}

std::string SupportedBuildsText() {
    const std::span<const GameBuild> builds = SupportedBuilds();
    std::string text;
    for (size_t i = 0; i < builds.size(); ++i) {
        if (i > 0) {
            text += i + 1 == builds.size() ? " and " : ", ";
        }
        text += builds[i].label;
    }
    return text;
}

} // namespace tsr
