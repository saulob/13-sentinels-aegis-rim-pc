#include "game_profile.h"

#include <string_view>

namespace tsr {
namespace {

constexpr PackageLayer kBaseLayers[] = {{"01.00", "01.00", false}};
constexpr PackageLayer kUpdate101Layers[] = {{"01.00", "01.00", false}, {"01.01", "01.00", true}};
constexpr PackageLayer kUpdate102Layers[] = {{"01.00", "01.00", false}, {"01.02", "01.00", true}};
constexpr PackageLayer kEuropeBaseLayers[] = {{"01.00", "01.01", false}};
constexpr PackageLayer kEuropeUpdate101Layers[] = {{"01.00", "01.01", false}, {"01.01", "01.00", true}};
// Each Japanese update is laid over the base alone: 1.04 holds everything of 1.03 and no longer opens ROBO_PATCH2.CPK.
constexpr PackageLayer kJapanUpdate103Layers[] = {{"01.00", "01.00", false}, {"01.03", "01.00", true}};
constexpr PackageLayer kJapanUpdate104Layers[] = {{"01.00", "01.00", false}, {"01.04", "01.00", true}};
constexpr PackageLayer kAsiaBaseLayers[] = {{"01.00", "01.01", false}};

constexpr const char* kBaseFiles[] = {"ROBO_FR.CPK", "ROBO_GE.CPK", "ROBO_IT.CPK", "ROBO_SP.CPK"};
// Each update is laid over the base alone. 1.01 opens ROBO_PATCH_US.CPK, 1.02 also ROBO_PATCH2%s.CPK.
constexpr const char* kUpdate101Files[] = {"ROBO_FR.CPK", "ROBO_GE.CPK", "ROBO_IT.CPK", "ROBO_SP.CPK",
                                           "ROBO_PATCH_US.CPK", "sce_discmap_patch.plt"};
constexpr const char* kUpdate102Files[] = {
    "ROBO_FR.CPK",         "ROBO_GE.CPK",        "ROBO_IT.CPK",        "ROBO_SP.CPK",
    "ROBO_PATCH_US.CPK",   "ROBO_PATCH2_US.CPK", "ROBO_PATCH2_FR.CPK", "ROBO_PATCH2_GE.CPK",
    "ROBO_PATCH2_IT.CPK",  "ROBO_PATCH2_SP.CPK", "sce_discmap_patch.plt",
};

constexpr const char* kJapanUpdate103Files[] = {"ROBO_PATCH.CPK", "ROBO_PATCH2.CPK", "sce_discmap_patch.plt"};
constexpr const char* kJapanUpdate104Files[] = {"ROBO_PATCH.CPK", "sce_discmap_patch.plt"};
constexpr const char* kAsiaFiles[] = {"ROBO_TW.CPK"};
// EUR 1.01 also comes repackaged for older firmware: the same game, its eboot.bin differing only in the SDK version.
constexpr const char* kEuropeUpdate101Alternates[] = {"123730cd2ee9b1ebbed4a6cc2fe7854154aad82645ef12da15d7e34b82da6b28"};

constexpr const char* kBasePromptArchives[] = {"ROBO_US.CPK", "ROBO_FR.CPK", "ROBO_GE.CPK", "ROBO_IT.CPK", "ROBO_SP.CPK"};
// 1.02 reads its button atlas from the ROBO_PATCH2 archives and the manual pages from the base ones.
constexpr const char* kUpdatePromptArchives[] = {"ROBO_US.CPK",        "ROBO_FR.CPK",        "ROBO_GE.CPK",
                                                 "ROBO_IT.CPK",        "ROBO_SP.CPK",        "ROBO_PATCH2_US.CPK",
                                                 "ROBO_PATCH2_FR.CPK", "ROBO_PATCH2_GE.CPK", "ROBO_PATCH2_IT.CPK",
                                                 "ROBO_PATCH2_SP.CPK"};

constexpr const char* kJapanBasePromptArchives[] = {"ROBO.CPK"};
constexpr const char* kJapanUpdatePromptArchives[] = {"ROBO.CPK", "ROBO_PATCH.CPK"};
constexpr const char* kAsiaPromptArchives[] = {"ROBO_CN.CPK", "ROBO_TW.CPK"};

constexpr TextLanguage kLanguages[] = {
    {"english", "English", 1, "ROBO_US.CPK"}, {"french", "French", 2, "ROBO_FR.CPK"}, {"german", "German", 4, "ROBO_GE.CPK"},
    {"italian", "Italian", 5, "ROBO_IT.CPK"}, {"spanish", "Spanish", 3, "ROBO_SP.CPK"},
};

constexpr TextLanguage kJapaneseLanguages[] = {{"japanese", "Japanese", 0, "ROBO.CPK"}};
// The Asian executable mounts ROBO_CN.CPK alone for Simplified Chinese and lays ROBO_TW.CPK over it for any other
// language, Traditional Chinese being its own default.
constexpr TextLanguage kAsianLanguages[] = {
    {"tchinese", "Traditional Chinese", 10, "ROBO_TW.CPK"},
    {"schinese", "Simplified Chinese", 11, "ROBO_CN.CPK"},
};

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

constexpr TsrResolutionSite kResolutionSitesJapan100[] = {
    {0x614f9f, 1, {0xb8}, 1920, kWidth},  {0x614fa4, 1, {0xb9}, 3840, kWidth},
    {0x614fba, 1, {0xb8}, 1080, kHeight}, {0x614fbf, 1, {0xb9}, 2160, kHeight},
    {0x61517e, 1, {0xb8}, 1080, kHeight}, {0x615183, 1, {0xb9}, 2160, kHeight},
    {0x615199, 1, {0xb8}, 1920, kWidth},  {0x61519e, 1, {0xb9}, 3840, kWidth},
    {0x615a6c, 1, {0xb9}, 3840, kWidth},  {0x615a71, 1, {0xb8}, 1920, kWidth},
    {0x615a8c, 1, {0xb9}, 2160, kHeight}, {0x615a91, 1, {0xb8}, 1080, kHeight},
};

constexpr TsrResolutionSite kResolutionSitesJapan103[] = {
    {0x681eb1, 1, {0xb8}, 1920, kWidth},  {0x681eb6, 1, {0xb9}, 3840, kWidth},
    {0x681ecc, 1, {0xb8}, 1080, kHeight}, {0x681ed1, 1, {0xb9}, 2160, kHeight},
    {0x682008, 1, {0xb8}, 1080, kHeight}, {0x68200d, 1, {0xb9}, 2160, kHeight},
    {0x682023, 1, {0xb8}, 1920, kWidth},  {0x682028, 1, {0xb9}, 3840, kWidth},
    {0x6827bc, 1, {0xb9}, 3840, kWidth},  {0x6827c1, 1, {0xb8}, 1920, kWidth},
    {0x6827dc, 1, {0xb9}, 2160, kHeight}, {0x6827e1, 1, {0xb8}, 1080, kHeight},
};

constexpr TsrResolutionSite kResolutionSitesJapan104[] = {
    {0x5bb28f, 1, {0xb8}, 1920, kWidth},  {0x5bb294, 1, {0xb9}, 3840, kWidth},
    {0x5bb2aa, 1, {0xb8}, 1080, kHeight}, {0x5bb2af, 1, {0xb9}, 2160, kHeight},
    {0x5bb3e6, 1, {0xb8}, 1080, kHeight}, {0x5bb3eb, 1, {0xb9}, 2160, kHeight},
    {0x5bb401, 1, {0xb8}, 1920, kWidth},  {0x5bb406, 1, {0xb9}, 3840, kWidth},
    {0x5bbb7c, 1, {0xb9}, 3840, kWidth},  {0x5bbb81, 1, {0xb8}, 1920, kWidth},
    {0x5bbb9c, 1, {0xb9}, 2160, kHeight}, {0x5bbba1, 1, {0xb8}, 1080, kHeight},
};

constexpr TsrResolutionSite kResolutionSitesAsia100[] = {
    {0x6878bf, 1, {0xb8}, 1920, kWidth},  {0x6878c4, 1, {0xb9}, 3840, kWidth},
    {0x6878da, 1, {0xb8}, 1080, kHeight}, {0x6878df, 1, {0xb9}, 2160, kHeight},
    {0x687a9e, 1, {0xb8}, 1080, kHeight}, {0x687aa3, 1, {0xb9}, 2160, kHeight},
    {0x687ab9, 1, {0xb8}, 1920, kWidth},  {0x687abe, 1, {0xb9}, 3840, kWidth},
    {0x68839c, 1, {0xb9}, 3840, kWidth},  {0x6883a1, 1, {0xb8}, 1920, kWidth},
    {0x6883bc, 1, {0xb9}, 2160, kHeight}, {0x6883c1, 1, {0xb8}, 1080, kHeight},
};

constexpr char kUsaContent[] = "UP0177-CUSA19610_00-13SENTINELSEFIGS";
constexpr char kEuropeContent[] = "EP0177-CUSA19620_00-13SENTINELSEFIGS";
constexpr char kJapanContent[] = "JP0005-CUSA10602_00-13K0000000000000";
constexpr char kAsiaContent[] = "HP0177-CUSA18093_00-13K0000000000000";

constexpr GameBuild kGameBuilds[] = {
    {.title_id = "CUSA19610", .content_id = kUsaContent, .version = "1.00", .folder = "CUSA19610-1.00",
     .eboot_sha256 = "cf7e90939842b3c9c02f9116e26c2e398610312217fea05d52a1595c60ed5950", .regional_archive = "ROBO_US.CPK",
     .extra_files = kBaseFiles, .label = "CUSA19610 1.00 USA", .region = "USA", .layers = kBaseLayers,
     .resolution_sites = kResolutionSites100, .text_languages = kLanguages,
     .prompt_archives = kBasePromptArchives, .english_voices = false, .prompt_patch = "1.00", .voice_archive = "ROBO_US.CPK"},
    {.title_id = "CUSA19610", .content_id = kUsaContent, .version = "1.01", .folder = "CUSA19610-1.01",
     .eboot_sha256 = "dafcc5f005da0eb74f7289c6dad4e8b8079a400e4832b3bc0a47b1c26a050533", .regional_archive = "ROBO_US.CPK",
     .extra_files = kUpdate101Files, .label = "CUSA19610 1.01 USA", .region = "USA", .layers = kUpdate101Layers,
     .resolution_sites = kResolutionSites101, .text_languages = kLanguages,
     .prompt_archives = kBasePromptArchives, .english_voices = true, .prompt_patch = "1.00", .voice_archive = "ROBO_US.CPK",
     .english_voice_archive = "ROBO_PATCH_US.CPK"},
    {.title_id = "CUSA19610", .content_id = kUsaContent, .version = "1.02", .folder = "CUSA19610-1.02",
     .eboot_sha256 = "075250099ba56c2e8f35b4dc8d35b25c5f98304e45c8546c790d56fe87803ff1", .regional_archive = "ROBO_US.CPK",
     .extra_files = kUpdate102Files, .label = "CUSA19610 1.02 USA", .region = "USA", .layers = kUpdate102Layers,
     .resolution_sites = kResolutionSites102, .text_languages = kLanguages,
     .prompt_archives = kUpdatePromptArchives, .english_voices = true, .prompt_patch = "1.02", .voice_archive = "ROBO_US.CPK",
     .english_voice_archive = "ROBO_PATCH_US.CPK"},
    // The European executables are the American ones: 1.00 differs only in its build date string, 1.01 not at all.
    {.title_id = "CUSA19620", .content_id = kEuropeContent, .version = "1.00", .folder = "CUSA19620-1.00",
     .eboot_sha256 = "e4df1d07de8afac5f55c91ea5f54f86f8b3c9999ca7e2ac3d602d04ea11490e1", .regional_archive = "ROBO_US.CPK",
     .extra_files = kBaseFiles, .label = "CUSA19620 1.00 Europe", .region = "Europe", .layers = kEuropeBaseLayers,
     .resolution_sites = kResolutionSites100, .text_languages = kLanguages,
     .prompt_archives = kBasePromptArchives, .english_voices = false, .prompt_patch = "eu-1.00", .voice_archive = "ROBO_US.CPK",
     .code_build = "CUSA19610-1.00"},
    {.title_id = "CUSA19620", .content_id = kEuropeContent, .version = "1.01", .folder = "CUSA19620-1.01",
     .eboot_sha256 = "dafcc5f005da0eb74f7289c6dad4e8b8079a400e4832b3bc0a47b1c26a050533", .regional_archive = "ROBO_US.CPK",
     .extra_files = kUpdate101Files, .label = "CUSA19620 1.01 Europe", .region = "Europe", .layers = kEuropeUpdate101Layers,
     .resolution_sites = kResolutionSites101, .text_languages = kLanguages,
     .prompt_archives = kBasePromptArchives, .english_voices = true, .prompt_patch = "eu-1.00", .voice_archive = "ROBO_US.CPK",
     .english_voice_archive = "ROBO_PATCH_US.CPK", .code_build = "CUSA19610-1.01", .alternate_eboots = kEuropeUpdate101Alternates},
    {.title_id = "CUSA10602", .content_id = kJapanContent, .version = "1.00", .folder = "CUSA10602-1.00",
     .eboot_sha256 = "6d6f67ce332091875c56d211a60d1158075e6516afc0b188029ed420dee85d0c", .regional_archive = "ROBO.CPK",
     .extra_files = {}, .label = "CUSA10602 1.00 Japan", .region = "Japan", .layers = kBaseLayers,
     .resolution_sites = kResolutionSitesJapan100, .text_languages = kJapaneseLanguages,
     .prompt_archives = kJapanBasePromptArchives, .english_voices = false, .prompt_patch = "jp-1.00", .voice_archive = "ROBO.CPK"},
    {.title_id = "CUSA10602", .content_id = kJapanContent, .version = "1.03", .folder = "CUSA10602-1.03",
     .eboot_sha256 = "fdd1b5ad5d8a42caf81397f19dc53914bcc927fe06fd077845d425528e07c11a", .regional_archive = "ROBO.CPK",
     .extra_files = kJapanUpdate103Files, .label = "CUSA10602 1.03 Japan", .region = "Japan", .layers = kJapanUpdate103Layers,
     .resolution_sites = kResolutionSitesJapan103, .text_languages = kJapaneseLanguages,
     .prompt_archives = kJapanUpdatePromptArchives, .english_voices = false, .prompt_patch = "jp-1.03",
     .voice_archive = "ROBO_PATCH.CPK"},
    {.title_id = "CUSA10602", .content_id = kJapanContent, .version = "1.04", .folder = "CUSA10602-1.04",
     .eboot_sha256 = "3bcaeac50607e4ded31b5123a4170f646bdb0441a070fe9ce6757dfd60c4fbcb", .regional_archive = "ROBO.CPK",
     .extra_files = kJapanUpdate104Files, .label = "CUSA10602 1.04 Japan", .region = "Japan", .layers = kJapanUpdate104Layers,
     .resolution_sites = kResolutionSitesJapan104, .text_languages = kJapaneseLanguages,
     .prompt_archives = kJapanUpdatePromptArchives, .english_voices = true, .prompt_patch = "jp-1.04",
     .voice_archive = "ROBO_PATCH.CPK", .english_voice_archive = "ROBO_PATCH.CPK"},
    {.title_id = "CUSA18093", .content_id = kAsiaContent, .version = "1.00", .folder = "CUSA18093-1.00",
     .eboot_sha256 = "dd6d54e99b1246dbecd1fa9556bf90e58fe6ff5cfdf01f010bfd9a4c6c572bcf", .regional_archive = "ROBO_CN.CPK",
     .extra_files = kAsiaFiles, .label = "CUSA18093 1.00 Asia", .region = "Asia", .layers = kAsiaBaseLayers,
     .resolution_sites = kResolutionSitesAsia100, .text_languages = kAsianLanguages,
     .prompt_archives = kAsiaPromptArchives, .english_voices = false, .prompt_patch = "asia-1.00", .voice_archive = "ROBO_CN.CPK"},
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
        if (i > 0 && std::string_view(builds[i].title_id) == builds[i - 1].title_id) {
            text += ", ";
        } else {
            text += i == 0 ? "" : "; ";
            text += std::string(builds[i].title_id) + " " + builds[i].region + " ";
        }
        text += builds[i].version;
    }
    return text;
}

bool IsBuildExecutable(const GameBuild& build, std::string_view sha256) {
    if (sha256 == build.eboot_sha256) {
        return true;
    }
    for (const char* alternate : build.alternate_eboots) {
        if (sha256 == alternate) {
            return true;
        }
    }
    return false;
}

std::string CodeBuildKey(const GameBuild& build) {
    return build.code_build[0] ? std::string(build.code_build) : std::string(build.title_id) + "-" + build.version;
}

} // namespace tsr
