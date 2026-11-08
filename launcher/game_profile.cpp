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

// The game's three sceKernelIsNeoMode choices between 1920 x 1080 and 3840 x 2160: its display colour target, its depth
// target and the screen size getters every viewport and scene target reads (`mov reg, imm32`, both alternatives).
constexpr int kWidth = 0;
constexpr int kHeight = 1;

constexpr TsrResolutionSite kResolutionSites100[] = {
    {0x681781, 1, {0xb8}, 1920, kWidth},  {0x681786, 1, {0xb9}, 3840, kWidth},
    {0x68179c, 1, {0xb8}, 1080, kHeight}, {0x6817a1, 1, {0xb9}, 2160, kHeight},
    {0x6818d8, 1, {0xb8}, 1080, kHeight}, {0x6818dd, 1, {0xb9}, 2160, kHeight},
    {0x6818f3, 1, {0xb8}, 1920, kWidth},  {0x6818f8, 1, {0xb9}, 3840, kWidth},
    {0x68208c, 1, {0xb9}, 3840, kWidth},  {0x682091, 1, {0xb8}, 1920, kWidth},
    {0x6820ac, 1, {0xb9}, 2160, kHeight}, {0x6820b1, 1, {0xb8}, 1080, kHeight},
};

constexpr TsrResolutionSite kResolutionSites101[] = {
    {0x683a11, 1, {0xb8}, 1920, kWidth},  {0x683a16, 1, {0xb9}, 3840, kWidth},
    {0x683a2c, 1, {0xb8}, 1080, kHeight}, {0x683a31, 1, {0xb9}, 2160, kHeight},
    {0x683b68, 1, {0xb8}, 1080, kHeight}, {0x683b6d, 1, {0xb9}, 2160, kHeight},
    {0x683b83, 1, {0xb8}, 1920, kWidth},  {0x683b88, 1, {0xb9}, 3840, kWidth},
    {0x68431c, 1, {0xb9}, 3840, kWidth},  {0x684321, 1, {0xb8}, 1920, kWidth},
    {0x68433c, 1, {0xb9}, 2160, kHeight}, {0x684341, 1, {0xb8}, 1080, kHeight},
};

constexpr TsrResolutionSite kResolutionSites102[] = {
    {0x5bcfff, 1, {0xb8}, 1920, kWidth},  {0x5bd004, 1, {0xb9}, 3840, kWidth},
    {0x5bd01a, 1, {0xb8}, 1080, kHeight}, {0x5bd01f, 1, {0xb9}, 2160, kHeight},
    {0x5bd156, 1, {0xb8}, 1080, kHeight}, {0x5bd15b, 1, {0xb9}, 2160, kHeight},
    {0x5bd171, 1, {0xb8}, 1920, kWidth},  {0x5bd176, 1, {0xb9}, 3840, kWidth},
    {0x5bd8ec, 1, {0xb9}, 3840, kWidth},  {0x5bd8f1, 1, {0xb8}, 1920, kWidth},
    {0x5bd90c, 1, {0xb9}, 2160, kHeight}, {0x5bd911, 1, {0xb8}, 1080, kHeight},
};

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
        kResolutionSites100,
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
        kResolutionSites101,
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
        kResolutionSites102,
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
