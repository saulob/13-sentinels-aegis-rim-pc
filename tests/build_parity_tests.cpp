// The parity gate: every supported build of every region keeps its identity, package layering, executable features,
// languages, voices, Xbox icons, Exit Game labels and report identity. A build or a feature dropped from any table fails
// here. With the real boot images (build\image, or TSR_TEST_IMAGES), every feature site is also located on the real code
// and no two features may touch the same bytes.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "controller_prompts.h"
#include "cpk_patch.h"
#include "exit_game.h"
#include "game_files.h"
#include "game_profile.h"
#include "language_support.h"
#include "lzcnt_compat.h"
#include "skip_intro.h"
#include "tsr_resolution.h"
#include "tsr_title.h"
#include "tsrgpu.h"

namespace fs = std::filesystem;
using namespace tsr;

namespace {

int checks = 0, failures = 0;
#define CHECK(condition)                                                                                                   \
    do {                                                                                                                   \
        ++checks;                                                                                                          \
        if (!(condition)) {                                                                                                \
            ++failures;                                                                                                    \
            std::printf("FAIL line %d (%s): %s\n", __LINE__, current, #condition);                                         \
        }                                                                                                                  \
    } while (0)

const char* current = "";

struct Contract {
    const char* key;
    const char* label;
    const char* content_id;
    const char* app_ver;
    const char* sfo_version;
    const char* base_key; // empty for a base build
    const char* eboot_sha256;
    const char* code_key;
    const char* languages[3];
    bool english_voices;
    const char* prompt_patch;
    uint64_t image_size, image_entry; // the boot image identity LZCNT recognizes
    size_t lzcnt_sites;
};

constexpr char kUsa[] = "UP0177-CUSA19610_00-13SENTINELSEFIGS";
constexpr char kEurope[] = "EP0177-CUSA19620_00-13SENTINELSEFIGS";
constexpr char kJapan[] = "JP0005-CUSA10602_00-13K0000000000000";
constexpr char kAsia[] = "HP0177-CUSA18093_00-13K0000000000000";
#define WESTERN {"english", "french", "german"}

const Contract kContracts[] = {
    {"CUSA19610-1.00", "CUSA19610 1.00 USA", kUsa, "01.00", "01.00", "",
     "cf7e90939842b3c9c02f9116e26c2e398610312217fea05d52a1595c60ed5950", "CUSA19610-1.00", WESTERN, false, "1.00",
     0x19c5150, 0x404cc0, 75},
    {"CUSA19610-1.01", "CUSA19610 1.01 USA", kUsa, "01.01", "01.00", "CUSA19610-1.00",
     "dafcc5f005da0eb74f7289c6dad4e8b8079a400e4832b3bc0a47b1c26a050533", "CUSA19610-1.01", WESTERN, true, "1.00",
     0x19c5150, 0x406220, 75},
    {"CUSA19610-1.02", "CUSA19610 1.02 USA", kUsa, "01.02", "01.00", "CUSA19610-1.00",
     "075250099ba56c2e8f35b4dc8d35b25c5f98304e45c8546c790d56fe87803ff1", "CUSA19610-1.02", WESTERN, true, "1.02",
     0x19a9150, 0x3bdbd0, 73},
    {"CUSA19620-1.00", "CUSA19620 1.00 Europe", kEurope, "01.00", "01.01", "",
     "e4df1d07de8afac5f55c91ea5f54f86f8b3c9999ca7e2ac3d602d04ea11490e1", "CUSA19610-1.00", WESTERN, false, "eu-1.00",
     0x19c5150, 0x404cc0, 75},
    {"CUSA19620-1.01", "CUSA19620 1.01 Europe", kEurope, "01.01", "01.00", "CUSA19620-1.00",
     "dafcc5f005da0eb74f7289c6dad4e8b8079a400e4832b3bc0a47b1c26a050533", "CUSA19610-1.01", WESTERN, true, "eu-1.00",
     0x19c5150, 0x406220, 75},
    {"CUSA10602-1.00", "CUSA10602 1.00 Japan", kJapan, "01.00", "01.00", "",
     "6d6f67ce332091875c56d211a60d1158075e6516afc0b188029ed420dee85d0c", "CUSA10602-1.00", {"japanese"}, false, "jp-1.00",
     0x236d160, 0x39cda0, 77},
    {"CUSA10602-1.03", "CUSA10602 1.03 Japan", kJapan, "01.03", "01.00", "CUSA10602-1.00",
     "fdd1b5ad5d8a42caf81397f19dc53914bcc927fe06fd077845d425528e07c11a", "CUSA10602-1.03", {"japanese"}, false, "jp-1.03",
     0x1bc5150, 0x404440, 75},
    {"CUSA10602-1.04", "CUSA10602 1.04 Japan", kJapan, "01.04", "01.00", "CUSA10602-1.00",
     "3bcaeac50607e4ded31b5123a4170f646bdb0441a070fe9ce6757dfd60c4fbcb", "CUSA10602-1.04", {"japanese"}, true, "jp-1.04",
     0x19a9150, 0x3bbe60, 73},
    {"CUSA18093-1.00", "CUSA18093 1.00 Asia", kAsia, "01.00", "01.01", "",
     "dd6d54e99b1246dbecd1fa9556bf90e58fe6ff5cfdf01f010bfd9a4c6c572bcf", "CUSA18093-1.00", {"tchinese", "schinese"}, false,
     "asia-1.00", 0x258d160, 0x40f200, 77},
};

const GameBuild* Find(std::string_view key) {
    const GameBuild* found = nullptr;
    for (const GameBuild& build : SupportedBuilds()) {
        if (BuildKey(build) == key) {
            CHECK(!found);
            found = &build;
        }
    }
    return found;
}

void Append32(std::string& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out += char(value >> (8 * i) & 0xff);
    }
}

std::string Sfo(std::initializer_list<std::pair<std::string, std::string>> fields) {
    std::string index, keys, values;
    for (const auto& [key, value] : fields) {
        index += char(keys.size() & 0xff);
        index += char(keys.size() >> 8);
        index += '\x04';
        index += '\x02';
        Append32(index, uint32_t(value.size() + 1));
        Append32(index, uint32_t(value.size() + 1));
        Append32(index, uint32_t(values.size()));
        keys += key + '\0';
        values += value + '\0';
    }
    const uint32_t key_table = uint32_t(20 + index.size());
    std::string out("\0PSF", 4);
    Append32(out, 0x101);
    Append32(out, key_table);
    Append32(out, uint32_t(key_table + keys.size()));
    Append32(out, uint32_t(fields.size()));
    return out + index + keys + values;
}

std::string PackageSfo(const char* title, const char* content, bool update, const char* app_ver, const char* version) {
    return Sfo({{"TITLE_ID", title}, {"CONTENT_ID", content}, {"CATEGORY", update ? "gp" : "gd"}, {"APP_VER", app_ver},
                {"VERSION", version}});
}

void CheckIdentity(const Contract& c, const GameBuild& build) {
    CHECK(std::string_view(build.label) == c.label);
    CHECK(std::string(build.title_id) + " " + build.version + " " + build.region == c.label); // log, Play page, report
    CHECK(std::string_view(build.content_id) == c.content_id);
    CHECK(std::string_view(build.folder) == c.key);
    CHECK(std::string_view(build.eboot_sha256) == c.eboot_sha256 && IsBuildExecutable(build, c.eboot_sha256));
    CHECK(CodeBuildKey(build) == c.code_key);
    const PackageLayer& finished = FinishedLayer(build);
    CHECK(std::string_view(finished.app_ver) == c.app_ver && std::string_view(finished.sfo_version) == c.sfo_version);
    CHECK(finished.update == (c.base_key[0] != 0) && IsUpdateBuild(build) == (c.base_key[0] != 0));
    const GameBuild* base = BaseBuildOf(build);
    CHECK(c.base_key[0] ? base && BuildKey(*base) == c.base_key : !base);
    CHECK(SupportedBuildsText().find(std::string(build.title_id) + " " + build.region) != std::string::npos);

    // Package recognition by metadata, never by file name: every layer is the package of its own build.
    for (size_t layer = 0; layer < build.layers.size(); ++layer) {
        const PackageLayer& l = build.layers[layer];
        const GameCheck identity = CheckPackageIdentity(PackageSfo(build.title_id, build.content_id, l.update, l.app_ver, l.sfo_version));
        CHECK(identity.state == GameFileState::kValid);
        if (layer + 1 == build.layers.size()) {
            CHECK(identity.build == &build && identity.layer == layer);
        } else {
            CHECK(identity.build == base && identity.layer == 0);
        }
    }
    const GameCheck finished_sfo = CheckSfoIdentity(PackageSfo(build.title_id, build.content_id, finished.update,
                                                               finished.app_ver, finished.sfo_version));
    CHECK(finished_sfo.build == &build);
}

void CheckLanguagesAndVoices(const Contract& c, const GameBuild& build) {
    size_t count = 0;
    for (const char* key : c.languages) {
        if (!key) {
            continue;
        }
        CHECK(count < build.text_languages.size() && build.text_languages[count].key == key);
        ++count;
    }
    if (std::string_view(c.languages[0]) == "english") {
        count = 5; // the five Western languages
    }
    CHECK(build.text_languages.size() == count);
    std::set<int> ids;
    for (const TextLanguage& language : build.text_languages) {
        ids.insert(language.ps4_id);
        CHECK(language.archive.size() > 4);
    }
    CHECK(ids.size() == build.text_languages.size());
    CHECK(build.english_voices == c.english_voices);
    CHECK(build.voice_archive[0] != 0);
    CHECK((build.english_voice_archive[0] != 0) == c.english_voices);
    CHECK(JapaneseVoicesOf(build).archive == std::string_view(build.voice_archive));
}

void CheckPrompts(const Contract& c, const GameBuild& build) {
    CHECK(std::string_view(build.prompt_patch) == c.prompt_patch);
    const std::span<const uint8_t> bytes = XboxPromptsPatch(build);
    CHECK(!bytes.empty());
    const std::optional<BlockPatch> patch = ParseBlockPatch(bytes);
    CHECK(patch && !patch->targets.empty());
    if (!patch) {
        return;
    }
    std::set<std::string> covered;
    for (const BlockPatchTarget& target : patch->targets) {
        CHECK(std::find(build.prompt_archives.begin(), build.prompt_archives.end(), target.archive) != build.prompt_archives.end());
        covered.insert(target.archive);
    }
    CHECK(covered.size() == build.prompt_archives.size());
}

void CheckExecutableFeatures(const Contract& c, const GameBuild& build) {
    CHECK(build.resolution_sites.size() == 12 && !RestrictedToNativeResolution(&build));
    // A known build key with code that is not there is refused as a mismatch; an unknown key would mean it was dropped.
    std::vector<uint8_t> blank(0x10000);
    uint32_t site = 0;
    CHECK(skip_intro_locate(blank.data(), blank.size(), c.code_key, &site) == SKIP_INTRO_MISMATCH);
    CHECK(exit_game_locate(blank.data(), blank.size(), c.code_key, nullptr, nullptr) == EXIT_GAME_SITE_MISMATCH);
    const LzcntBuild* lzcnt = nullptr;
    for (const LzcntBuild& known : lzcnt_builds) {
        if (known.image.size == c.image_size && known.image.entry == c.image_entry) {
            CHECK(!lzcnt);
            lzcnt = &known;
        }
    }
    CHECK(lzcnt && lzcnt->count == c.lzcnt_sites && std::string_view(lzcnt->build) == c.code_key);
}

void CheckExitGameLabels(const GameBuild& build) {
    size_t size = 0;
    const uint8_t* data = exit_game_embedded_asset(&size);
    static ExitGameAsset asset;
    CHECK(exit_game_asset_parse(data, size, &asset));
    for (const TextLanguage& language : build.text_languages) {
        bool found = false;
        for (uint32_t i = 0; i < asset.count; ++i) {
            found = found || language.key == asset.languages[i].key;
        }
        CHECK(found);
    }
}

void TestContracts() {
    std::set<std::string> folders, labels;
    for (const GameBuild& build : SupportedBuilds()) {
        folders.insert(build.folder);
        labels.insert(build.label);
    }
    CHECK(SupportedBuilds().size() == std::size(kContracts));
    CHECK(folders.size() == SupportedBuilds().size() && labels.size() == SupportedBuilds().size());
    for (const Contract& c : kContracts) {
        current = c.key;
        const GameBuild* build = Find(c.key);
        CHECK(build);
        if (!build) {
            continue;
        }
        CheckIdentity(c, *build);
        CheckLanguagesAndVoices(c, *build);
        CheckPrompts(c, *build);
        CheckExecutableFeatures(c, *build);
        CheckExitGameLabels(*build);
    }
    current = "packages";
    // EUR 1.01 also comes repackaged for older firmware; both packagings are the one build.
    const GameBuild* europe = Find("CUSA19620-1.01");
    CHECK(europe && IsBuildExecutable(*europe, "123730cd2ee9b1ebbed4a6cc2fe7854154aad82645ef12da15d7e34b82da6b28"));
    // Wrong-region and unknown updates are refused by identity.
    CHECK(CheckPackageIdentity(PackageSfo("CUSA10602", kEurope, true, "01.04", "01.00")).state != GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo("CUSA19620", kEurope, true, "01.02", "01.00")).state != GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo("CUSA18093", kAsia, true, "01.01", "01.00")).state != GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo("CUSA10602", kJapan, true, "01.01", "01.00")).state != GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo("CUSA10602", kJapan, false, "01.04", "01.00")).state != GameFileState::kValid);
    current = "";
}

// The image the loader runs, followed by room for hook stubs within a rel32 jump.
struct Image {
    std::vector<uint8_t> bytes;
    uint64_t size = 0, entry = 0;
};

uint64_t Le64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = v << 8 | p[i];
    }
    return v;
}

bool LoadImage(const GameBuild& build, Image& image) {
    const char* extra = std::getenv("TSR_TEST_IMAGES");
    const fs::path folder = extra && *extra ? fs::path(extra) / build.folder
                                            : fs::path(TSR_SOURCE_DIR) / "build" / "image" / build.folder;
    std::ifstream in(folder / "boot.bin", std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (file.size() < 8 * 16 || std::memcmp(file.data(), "TSRBOOT1", 8)) {
        return false;
    }
    image.size = Le64(&file[8]);
    image.entry = Le64(&file[16]);
    if (image.size > file.size()) {
        return false;
    }
    image.bytes.assign(file.end() - std::ptrdiff_t(image.size), file.end());
    image.bytes.resize(image.size + (1u << 20), 0xcc);
    return true;
}

using Range = std::pair<uint64_t, uint64_t>;

void TestRealImages() {
    size_t verified = 0;
    for (const GameBuild& build : SupportedBuilds()) {
        current = build.label;
        Image image;
        if (!LoadImage(build, image)) {
            const char* required = std::getenv("TSR_TEST_IMAGES");
            CHECK(!required || !*required);
            std::printf("skipped: no %s boot image (build\\image or TSR_TEST_IMAGES)\n", build.label);
            continue;
        }
        const std::string code = CodeBuildKey(build);
        std::vector<std::pair<std::string, Range>> ranges;
        uint32_t intro = 0;
        CHECK(skip_intro_locate(image.bytes.data(), image.size, code.c_str(), &intro) == SKIP_INTRO_FOUND);
        ranges.push_back({"Skip Intro", {intro, intro + 5}});
        ExitGameSites exit{};
        CHECK(exit_game_locate(image.bytes.data(), image.size, code.c_str(), &exit, nullptr) == EXIT_GAME_FOUND);
        for (uint32_t read : exit.flag_reads) {
            ranges.push_back({"Exit Game flag read", {read, read + 5}});
        }
        ranges.push_back({"Exit Game confirm", {exit.confirm_dispatch, exit.confirm_dispatch + 6}});
        ranges.push_back({"Exit Game layout", {exit.layout_bind, exit.layout_bind + 7}});
        ranges.push_back({"Exit Game texture", {exit.texture_copy, exit.texture_copy + 5}});
        for (const TsrResolutionSite& site : build.resolution_sites) {
            ranges.push_back({"Render resolution", {site.offset, site.offset + site.opcode_length + 4}});
        }
        const LzcntBuild* lzcnt = nullptr;
        for (const LzcntBuild& known : lzcnt_builds) {
            if (known.image.size == image.size && known.image.entry == image.entry) {
                lzcnt = &known;
            }
        }
        CHECK(lzcnt);
        if (lzcnt) {
            for (size_t i = 0; i < lzcnt->count; ++i) {
                ranges.push_back({"LZCNT", {lzcnt->offsets[i], lzcnt->offsets[i] + lzcnt->shapes[i].length}});
            }
        }
        // No two patches touch the same bytes (sites of one feature are checked by the feature itself).
        std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        for (size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i].second.first < ranges[i - 1].second.second && ranges[i].first != ranges[i - 1].first) {
                std::printf("FAIL %s: %s at %#llx overlaps %s\n", build.label, ranges[i].first.c_str(),
                            static_cast<unsigned long long>(ranges[i].second.first), ranges[i - 1].first.c_str());
                ++failures;
            }
        }
        // Every feature applies to the real code: the 4K sites, then the LZCNT rewrite of the same image.
        char error[256] = "";
        CHECK(tsr_apply_resolution(image.bytes.data(), image.size, build.resolution_sites.data(), build.resolution_sites.size(),
                                   &tsr_resolutions[TSR_RESOLUTION_COUNT - 1], error, sizeof(error)) == 1);
        if (lzcnt) {
            CHECK(lzcnt_compat_install(image.bytes.data(), image.size, image.entry, image.bytes.data() + image.size,
                                       lzcnt_compat_stub_bytes(), nullptr) == LZCNT_COMPAT_OK);
        }
        ++verified;
    }
    current = "";
    if (verified == SupportedBuilds().size()) {
        std::printf("real boot images: every build's Skip Intro, Exit Game, render size and LZCNT sites located, no overlap\n");
    }
}

// Within a region the newest prepared build plays; of several regions, the one installed last. Folders never mix.
void TestRegionPrecedence() {
    current = "region precedence";
    const fs::path scratch = fs::temp_directory_path() / "tsr-build-parity";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    const GameRoots roots{scratch / "prepared", scratch / "image"};
    const auto prepare = [&](const char* key, int seconds_ago) {
        const GameBuild* build = Find(key);
        const fs::path marker = BuildPaths(roots, *build).app0 / kPreparedMarker;
        fs::create_directories(marker.parent_path());
        std::ofstream(marker) << "{\"version\": \"" << build->version << "\", \"scope\": \"full\"}";
        fs::last_write_time(marker, fs::file_time_type::clock::now() - std::chrono::seconds(seconds_ago));
    };
    prepare("CUSA19610-1.00", 400);
    prepare("CUSA19610-1.02", 300);
    CHECK(LastPreparedBuild(roots) == Find("CUSA19610-1.02"));
    prepare("CUSA10602-1.04", 200);
    CHECK(LastPreparedBuild(roots) == Find("CUSA10602-1.04"));
    prepare("CUSA10602-1.00", 500);
    CHECK(LastPreparedBuild(roots) == Find("CUSA10602-1.04"));
    prepare("CUSA10602-1.03", 50);
    CHECK(LastPreparedBuild(roots) == Find("CUSA10602-1.04")); // an older update installed later never downgrades
    prepare("CUSA19620-1.01", 10);
    CHECK(LastPreparedBuild(roots) == Find("CUSA19620-1.01"));
    prepare("CUSA19610-1.01", 1);
    CHECK(LastPreparedBuild(roots) == Find("CUSA19610-1.02"));
    CHECK(PreparedBuilds(roots).size() == 7);
    std::set<fs::path> folders;
    for (const GameBuild* build : PreparedBuilds(roots)) {
        folders.insert(BuildPaths(roots, *build).app0);
    }
    CHECK(folders.size() == 7);
    fs::remove_all(scratch, ec);
    current = "";
}

std::string SourceText(const char* relative) {
    std::ifstream in(fs::path(TSR_SOURCE_DIR) / relative, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The game window carries one English title in every region, whatever the package's localized param.sfo TITLE says,
// then the running build (its APP_VER, not the package VERSION); the render resolution and frame rate follow.
void TestWindowTitle() {
    current = "window title";
    CHECK(std::string_view(TSR_WINDOW_TITLE) == "13 Sentinels: Aegis Rim");
    CHECK(std::string_view(kGameTitle) == TSR_WINDOW_TITLE);
    const std::pair<const char*, const char*> expected[] = {
        {"CUSA19610-1.00", "13 Sentinels: Aegis Rim | CUSA19610 1.00"},
        {"CUSA19610-1.01", "13 Sentinels: Aegis Rim | CUSA19610 1.01"},
        {"CUSA19610-1.02", "13 Sentinels: Aegis Rim | CUSA19610 1.02"},
        {"CUSA19620-1.00", "13 Sentinels: Aegis Rim | CUSA19620 1.00"},
        {"CUSA19620-1.01", "13 Sentinels: Aegis Rim | CUSA19620 1.01"},
        {"CUSA10602-1.00", "13 Sentinels: Aegis Rim | CUSA10602 1.00"},
        {"CUSA10602-1.03", "13 Sentinels: Aegis Rim | CUSA10602 1.03"},
        {"CUSA10602-1.04", "13 Sentinels: Aegis Rim | CUSA10602 1.04"},
        {"CUSA18093-1.00", "13 Sentinels: Aegis Rim | CUSA18093 1.00"},
    };
    CHECK(std::size(expected) == SupportedBuilds().size());
    for (const auto& [key, title] : expected) {
        current = key;
        const GameBuild* build = Find(key);
        CHECK(build != nullptr);
        if (!build) {
            continue;
        }
        const PackageLayer& finished = FinishedLayer(*build);
        CHECK(std::string("0") + build->version == finished.app_ver);
        // The title is the identity the loader logs ("TSR: game build ..."), never the image folder name.
        const std::string base = TsrTitle::Base(TSR_WINDOW_TITLE, std::string(build->title_id) + " " + build->version);
        CHECK(base == title);
        CHECK(std::string(build->label).rfind(std::string(build->title_id) + " " + build->version + " ", 0) == 0);
        const std::string version_title = std::string(TSR_WINDOW_TITLE) + " | " + build->title_id + " " +
                                          (finished.sfo_version + (finished.sfo_version[0] == '0'));
        CHECK(std::string_view(finished.sfo_version) == finished.app_ver ? version_title == base : version_title != base);
        CHECK(TsrTitle::Compose({.base = base}) == title);
        CHECK(TsrTitle::Compose({.base = base, .show_resolution = true, .width = 3840, .height = 2160}) ==
              std::string(title) + " | 3840x2160");
        CHECK(TsrTitle::Compose({.base = base, .show_fps = true, .fps = 59.99}) == std::string(title) + " | FPS: 59.99");
        CHECK(TsrTitle::Compose({.base = base, .show_resolution = true, .width = 3840, .height = 2160, .show_fps = true,
                                 .fps = 60.0}) == std::string(title) + " | 3840x2160 | FPS: 60.00");
        CHECK(TsrTitle::Compose({.base = base, .show_resolution = true, .show_fps = true}) == title);
        CHECK(TsrTitle::Compose({.base = base, .entry = " - Name: x_"}) == std::string(title) + " - Name: x_");
    }
    current = "window title";
    CHECK(Find("CUSA19620-1.00") && std::string_view(FinishedLayer(*Find("CUSA19620-1.00")).sfo_version) == "01.01");
    CHECK(Find("CUSA18093-1.00") && std::string_view(FinishedLayer(*Find("CUSA18093-1.00")).sfo_version) == "01.01");
    CHECK(TsrTitle::Base(TSR_WINDOW_TITLE, "") == TSR_WINDOW_TITLE);
    const std::string loader = SourceText("src/loader.c");
    CHECK(!loader.empty());
    CHECK(loader.find("sfo_value(sfo,\"TITLE\",") == std::string::npos);
    CHECK(loader.find("sfo_value(sfo,\"VERSION\",") == std::string::npos);
    CHECK(loader.find("snprintf(running,sizeof(running),\"%s %s\",game_build.title_id,game_build.version);") !=
          std::string::npos);
    CHECK(loader.find("TsrGpuConfig gpu={TSR_WINDOW_TITLE,running,") != std::string::npos);
    const std::string shim = SourceText("gpu/shim/tsrgpu.cpp");
    CHECK(shim.find("TsrTitle::Base(config->title ? config->title : TSR_WINDOW_TITLE, config->build ? config->build : \"\")") !=
          std::string::npos);
    CHECK(SourceText("gpu/shim/window.cpp").find(".base = base_title,") != std::string::npos);
    current = "";
}

} // namespace

int main() {
    TestContracts();
    TestRegionPrecedence();
    TestWindowTitle();
    TestRealImages();
    std::printf("build parity: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
