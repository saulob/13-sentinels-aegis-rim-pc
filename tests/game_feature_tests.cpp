// Controller icons, language support, build status and Skip Intro settings on the launcher side: the embedded Xbox
// patches, the patch applied to, switched on and reverted from copies of real archives of the installed game
// (game\prepared, only read), refusals, an earlier patch, rollback, shared hard links, voice banks and the settings. With no installed game the archive checks
// are skipped, never replaced by synthetic data. How the icons look in game stays a manual check.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "controller_prompts.h"
#include "cpk_patch.h"
#include "game_profile.h"
#include "settings.h"
#include "tsr_launcher.h"
#include "language_support.h"

namespace fs = std::filesystem;
using namespace tsr;

namespace {

int checks = 0;
int failures = 0;
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(condition)) {                                                                                            \
            ++failures;                                                                                                \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                                                   \
        }                                                                                                              \
    } while (0)

std::string Read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

const GameBuild& Build(const char* version) {
    for (const GameBuild& build : SupportedBuilds()) {
        if (std::string_view(build.version) == version) {
            return build;
        }
    }
    std::printf("FAIL: no build %s\n", version);
    std::exit(1);
}

bool IsManualEntry(std::string_view name) {
    return name.starts_with("Manual/Manual") && name.ends_with(".ftx");
}

bool Contains(std::span<const char* const> names, std::string_view name) {
    for (const char* candidate : names) {
        if (name == candidate) {
            return true;
        }
    }
    return false;
}

void TestEmbeddedPatches() {
    for (const char* version : {"1.00", "1.01", "1.02"}) {
        const GameBuild& build = Build(version);
        std::string error;
        const std::optional<BlockPatch> patch = ParseBlockPatch(XboxPromptsPatch(build), &error);
        CHECK(patch.has_value());
        if (!patch) {
            continue;
        }
        const bool update = std::string_view(version) == "1.02";
        size_t atlas = 0;
        for (const BlockPatchTarget& target : patch->targets) {
            CHECK(Contains(build.prompt_archives, target.archive));
            CHECK(target.crc_before != target.crc_after && !target.runs.empty());
            uint64_t end = 0;
            bool runs_ordered = true;
            for (const BlockRun& run : target.runs) {
                runs_ordered = runs_ordered && run.offset >= end && uint64_t(run.offset) + 16 <= target.size;
                end = uint64_t(run.offset) + 16;
            }
            CHECK(runs_ordered);
            if (IsManualEntry(target.name)) {
                CHECK(target.size == 2089424);
            } else {
                ++atlas;
                CHECK(target.name == (update ? "gui/interface.ftx" : "GUI/Interface.ftx"));
                CHECK(target.size == 5768336);
                CHECK(update == target.archive.starts_with("ROBO_PATCH2_"));
            }
        }
        CHECK(atlas == 5);
        for (const char* archive : build.prompt_archives) {
            size_t manual = 0;
            for (const BlockPatchTarget& target : PatchForArchive(*patch, archive).targets) {
                manual += IsManualEntry(target.name);
            }
            CHECK(!PatchForArchive(*patch, archive).targets.empty());
            CHECK(manual >= 10 || std::string_view(archive).starts_with("ROBO_PATCH2_"));
        }
    }
    CHECK(XboxPromptsPatch(Build("1.01")).data() == XboxPromptsPatch(Build("1.00")).data());
    CHECK(XboxPromptsPatch(Build("1.02")).data() != XboxPromptsPatch(Build("1.00")).data());
    CHECK(Build("1.00").prompt_archives.size() == 5 && Build("1.01").prompt_archives.size() == 5);
    CHECK(Build("1.02").prompt_archives.size() == 10);
    CHECK(Build("1.00").prompt_archives.front() == std::string_view("ROBO_US.CPK"));
    CHECK(Build("1.02").prompt_archives.back() == std::string_view("ROBO_PATCH2_SP.CPK"));

    const std::span<const uint8_t> bytes = XboxPromptsPatch(Build("1.00"));
    CHECK(!ParseBlockPatch(std::span<const uint8_t>()).has_value());
    CHECK(!ParseBlockPatch(bytes.first(bytes.size() - 1)).has_value());
    std::vector<uint8_t> trailing(bytes.begin(), bytes.end());
    trailing.push_back(0);
    CHECK(!ParseBlockPatch(trailing).has_value());
    std::vector<uint8_t> older(bytes.begin(), bytes.end());
    older[4] = '2';
    CHECK(!ParseBlockPatch(older).has_value());
}

void SetReadOnly(const fs::path& file, bool readonly) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(file.c_str());
    SetFileAttributesW(file.c_str(), readonly ? attributes | FILE_ATTRIBUTE_READONLY : attributes & ~DWORD(FILE_ATTRIBUTE_READONLY));
#else
    std::error_code ec;
    fs::permissions(file, fs::perms::owner_write, readonly ? fs::perm_options::remove : fs::perm_options::add, ec);
#endif
}

const char* const kOneArchive[] = {"ROBO_FR.CPK"};
const char* const kTwoArchives[] = {"ROBO_FR.CPK", "ROBO_GE.CPK"};

GameBuild BuildWith(std::span<const char* const> archives) {
    GameBuild build = Build("1.00");
    build.prompt_archives = archives;
    return build;
}

// A copy of the installed archive taken back to the original through its own undo record, in a folder of its own.
bool CopyOriginal(const fs::path& source_folder, const char* archive, const fs::path& folder) {
    std::error_code ec;
    if (!fs::is_regular_file(source_folder / archive, ec)) {
        return false;
    }
    fs::create_directories(folder);
    fs::copy_file(source_folder / archive, folder / archive, fs::copy_options::overwrite_existing);
    const fs::path undo = UndoPath(source_folder / archive);
    if (fs::exists(undo, ec)) {
        fs::copy_file(undo, UndoPath(folder / archive), fs::copy_options::overwrite_existing);
        if (!RevertBlockPatch(folder / archive).ok) {
            return false;
        }
    }
    return true;
}

bool AllOriginal(const fs::path& archive, const BlockPatch& patch) {
    const std::vector<EntryReport> reports = InspectArchive(archive, patch);
    return !reports.empty() && std::all_of(reports.begin(), reports.end(),
                                           [](const EntryReport& r) { return r.state == EntryState::kOriginal; });
}

// Real archives of the installed USA 1.00, copied: the patch applied, switched and reverted on the copies.
void TestRealArchives(const fs::path& scratch) {
    const fs::path game = fs::path(TSR_SOURCE_DIR) / "game" / "prepared" / "CUSA19610-1.00";
    const GameBuild one = BuildWith(kOneArchive), two = BuildWith(kTwoArchives);
    const std::span<const uint8_t> xbox = XboxPromptsPatch(one);
    const std::optional<BlockPatch> patch = ParseBlockPatch(xbox);
    const fs::path a = scratch / "a";
    if (!patch || !CopyOriginal(game, "ROBO_FR.CPK", a) || !CopyOriginal(game, "ROBO_GE.CPK", a)) {
        std::printf("skipped: no installed USA 1.00 (game\\prepared\\CUSA19610-1.00): real archive checks\n");
        return;
    }
    const fs::path fr = a / "ROBO_FR.CPK", ge = a / "ROBO_GE.CPK";
    if (!AllOriginal(fr, *patch) || !AllOriginal(ge, *patch)) {
        std::printf("skipped: the installed ROBO_FR/GE.CPK are not the original files after reverting: real archive checks\n");
        return;
    }
    const std::string original_fr = Read(fr), original_ge = Read(ge);
    std::error_code ec;
    const int failures_before = failures;

    CHECK(XboxPromptsAvailable(a, two, xbox));
    CHECK(InstalledPromptState(a, two, xbox) == PromptState::kPlayStation);
    for (int round = 0; round < 3; ++round) {
        CHECK(SetPromptStyle(a, two, PromptStyle::kXbox, xbox).ok);
        CHECK(InstalledPromptState(a, two, xbox) == PromptState::kXbox);
        CHECK(InstalledPromptStyle(a, two, xbox) == PromptStyle::kXbox);
        CHECK(SetPromptStyle(a, two, PromptStyle::kXbox, xbox).ok);
        CHECK(!AllOriginal(fr, *patch) && ArchiveHolds(fr, *patch) && ArchiveHolds(ge, *patch));
        CHECK(SetPromptStyle(a, two, PromptStyle::kPlayStation, xbox).ok);
        CHECK(InstalledPromptState(a, two, xbox) == PromptState::kPlayStation);
        CHECK(Read(fr) == original_fr && Read(ge) == original_ge);
        CHECK(!fs::exists(UndoPath(fr), ec) && !fs::exists(UndoPath(ge), ec));
    }

    // An entry that is not the file the patch was made for refuses the archive whole; so does a missing entry.
    BlockPatch wrong = *patch;
    for (BlockPatchTarget& target : wrong.targets) {
        if (target.archive == "ROBO_FR.CPK" && IsManualEntry(target.name)) {
            target.crc_before ^= 1;
            target.crc_after ^= 1;
            break;
        }
    }
    const PatchResult refused = ApplyBlockPatch(fr, wrong);
    CHECK(!refused.ok && refused.detail.find("refused") != std::string::npos);
    CHECK(Read(fr) == original_fr && !fs::exists(UndoPath(fr), ec));
    BlockPatch missing = *patch;
    for (BlockPatchTarget& target : missing.targets) {
        if (target.archive == "ROBO_FR.CPK" && IsManualEntry(target.name)) {
            target.name = "Manual/Manual99.ftx";
            break;
        }
    }
    const PatchResult not_there = ApplyBlockPatch(fr, missing);
    CHECK(!not_there.ok && not_there.detail.find("missing") != std::string::npos);
    CHECK(Read(fr) == original_fr && !fs::exists(UndoPath(fr), ec));

    // A patch of another kind already in the archive is detected, replaced on request, and taken back byte for byte.
    BlockPatch earlier = *patch;
    {
        CpkArchive archive;
        CHECK(archive.Open(fr));
        for (BlockPatchTarget& target : earlier.targets) {
            if (target.archive == "ROBO_FR.CPK" && target.name == "GUI/Interface.ftx") {
                std::vector<uint8_t> data = archive.Read(*archive.Find(target.name));
                for (const BlockRun& run : target.runs) {
                    std::copy(run.data.begin(), run.data.end(), data.begin() + run.offset);
                }
                data[target.runs.front().offset] ^= 0x55;
                target.runs.front().data[0] ^= 0x55;
                target.crc_after = Crc32(data);
            }
        }
        earlier.targets.erase(std::remove_if(earlier.targets.begin(), earlier.targets.end(),
                                             [](const BlockPatchTarget& t) {
                                                 return t.archive != "ROBO_FR.CPK" || t.name != "GUI/Interface.ftx";
                                             }),
                              earlier.targets.end());
    }
    CHECK(ApplyBlockPatch(fr, earlier).ok);
    CHECK(ArchiveState(fr) == ArchivePatchState::kPatched);
    CHECK(InstalledPromptState(a, one, xbox) == PromptState::kOther);
    CHECK(InstalledPromptStyle(a, one, xbox) == PromptStyle::kPlayStation);
    CHECK(XboxPromptsAvailable(a, one, xbox));
    CHECK(SetPromptStyle(a, one, PromptStyle::kXbox, xbox).ok);
    CHECK(InstalledPromptState(a, one, xbox) == PromptState::kXbox);
    CHECK(SetPromptStyle(a, one, PromptStyle::kPlayStation, xbox).ok);
    CHECK(Read(fr) == original_fr);

    // The second archive cannot be written: the first one is put back and nothing is reported as done.
    SetReadOnly(ge, true);
    const PromptsResult failed = SetPromptStyle(a, two, PromptStyle::kXbox, xbox);
    SetReadOnly(ge, false);
    CHECK(!failed.ok);
    CHECK(Read(fr) == original_fr && Read(ge) == original_ge);
    CHECK(!fs::exists(UndoPath(fr), ec) && !fs::exists(UndoPath(ge), ec));
    CHECK(InstalledPromptState(a, two, xbox) == PromptState::kPlayStation);

    // A hard-linked archive of another installed build is patched in place, with no copy, and reverted the same way.
    const fs::path b = scratch / "b", c = scratch / "c";
    fs::create_directories(b);
    fs::create_directories(c);
    fs::create_hard_link(fr, b / "ROBO_FR.CPK", ec);
    const bool linked = !ec;
    if (linked) {
        CHECK(SetPromptStyle(a, one, PromptStyle::kXbox, xbox, {}, {b}).ok);
        CHECK(fs::equivalent(fr, b / "ROBO_FR.CPK", ec));
        CHECK(InstalledPromptState(b, one, xbox) == PromptState::kXbox);
        CHECK(SetPromptStyle(b, one, PromptStyle::kPlayStation, xbox, {}, {a}).ok);
        CHECK(Read(fr) == original_fr && Read(b / "ROBO_FR.CPK") == original_fr);
        CHECK(fs::equivalent(fr, b / "ROBO_FR.CPK", ec));
        CHECK(!fs::exists(UndoPath(fr), ec) && !fs::exists(UndoPath(b / "ROBO_FR.CPK"), ec));

        // A link nobody told about: the archive is copied first and the other folders keep the original.
        fs::create_hard_link(fr, c / "ROBO_FR.CPK", ec);
        CHECK(SetPromptStyle(a, one, PromptStyle::kXbox, xbox, {}, {b}).ok);
        CHECK(!fs::equivalent(fr, b / "ROBO_FR.CPK", ec) && !fs::equivalent(fr, c / "ROBO_FR.CPK", ec));
        CHECK(Read(b / "ROBO_FR.CPK") == original_fr && Read(c / "ROBO_FR.CPK") == original_fr);
        CHECK(InstalledPromptState(b, one, xbox) == PromptState::kPlayStation);
        CHECK(SetPromptStyle(a, one, PromptStyle::kPlayStation, xbox).ok);
        CHECK(Read(fr) == original_fr);
    } else {
        std::printf("skipped: hard links unavailable in %s\n", scratch.string().c_str());
    }
    if (failures == failures_before) {
        std::printf("real archive: ROBO_FR/GE.CPK of USA 1.00 switched 3 times and restored byte for byte, refusals, "
                    "earlier patch, rollback and shared links checked\n");
    }
}

// The installed game's own archives, read only: whatever icon state they are in, the patch must fit them.
void TestInstalledPrompts() {
    const fs::path base = fs::path(TSR_SOURCE_DIR) / "game" / "prepared" / "CUSA19610-1.00";
    std::error_code ec;
    if (!fs::is_regular_file(base / "ROBO_US.CPK", ec)) {
        std::printf("skipped: no installed USA 1.00: prompt fit and voice banks\n");
    } else {
        const GameBuild& build = Build("1.00");
        const std::optional<BlockPatch> patch = ParseBlockPatch(XboxPromptsPatch(build));
        std::string why;
        CHECK(patch && PromptPatchFits(base, build, *patch, &why));
        if (!why.empty()) {
            std::printf("note: %s\n", why.c_str());
        }
        const PromptState state = InstalledPromptState(base, build, XboxPromptsPatch(build));
        std::printf("note: the installed USA 1.00 shows %s icons\n", state == PromptState::kPlayStation ? "PlayStation"
                    : state == PromptState::kXbox                                                      ? "Xbox"
                                                                                                       : "another patch's");
        CHECK(VoiceBankPresent(base, kJapaneseVoices));
        CHECK(!VoiceBankPresent(base, kEnglishVoices));
        CHECK(!EnglishVoicesAvailable(base, &build));
        CHECK(!EnglishVoicesAvailable(base, &Build("1.02")));
        const LanguageSupport support = LanguagesOf(base, &build);
        CHECK(support.text.size() == 5 && !support.english_voices);
    }
    const fs::path update = fs::path(TSR_SOURCE_DIR) / "game" / "prepared" / "CUSA19610-1.02";
    if (!fs::is_regular_file(update / "ROBO_PATCH_US.CPK", ec)) {
        std::printf("skipped: no installed USA 1.02 (game\\prepared\\CUSA19610-1.02): its prompt fit and English voices\n");
        return;
    }
    const GameBuild& build = Build("1.02");
    const std::optional<BlockPatch> patch = ParseBlockPatch(XboxPromptsPatch(build));
    CHECK(patch && PromptPatchFits(update, build, *patch));
    CHECK(VoiceBankPresent(update, kEnglishVoices));
    CHECK(EnglishVoicesAvailable(update, &build));
}

LanguageSupport Support(bool english_voices) {
    LanguageSupport support;
    support.text.assign(Build("1.00").text_languages.begin(), Build("1.00").text_languages.end());
    support.english_voices = english_voices;
    return support;
}

std::string ReadSource(const char* relative) {
    return Read(fs::path(TSR_SOURCE_DIR) / relative);
}

void TestLanguageModel(const fs::path& scratch) {
    // Synthetic: an empty folder offers the build's text languages and no English voices.
    const fs::path empty = scratch / "no-game";
    for (const char* version : {"1.00", "1.01", "1.02"}) {
        const LanguageSupport support = LanguagesOf(empty, &Build(version));
        CHECK(support.text.size() == 5 && support.text.front().key == "english");
        CHECK(!support.english_voices);
    }
    CHECK(!Build("1.00").english_voices && Build("1.01").english_voices && Build("1.02").english_voices);
    LanguageSupport english_only_text = Support(true);
    english_only_text.text.resize(1);
    CHECK(EffectiveTextLanguage(english_only_text, "german").key == "english");
    CHECK(EffectiveTextLanguage(Support(true), "german").ps4_id == 4);
    CHECK(EffectiveTextLanguage(Support(false), "spanish").ps4_id == 3);
}

// The loader and the build no longer carry a voice override: the game's own Audio Language option decides.
void TestNoVoiceOverride() {
    const fs::path root = fs::path(TSR_SOURCE_DIR);
    std::error_code ec;
    CHECK(!fs::exists(root / "src" / "voice_language.c", ec) && !fs::exists(root / "src" / "voice_language.h", ec));
    const std::string loader = ReadSource("src/loader.c");
    CHECK(!loader.empty());
    CHECK(loader.find("voice_language") == std::string::npos && loader.find("tsr_voice") == std::string::npos);
    CHECK(ReadSource("CMakeLists.txt").find("voice_language") == std::string::npos);
    CHECK(ReadSource("launcher/tsr_launcher.h").find("voice") == std::string::npos);
    CHECK(ReadSource("launcher/settings.cpp").find("kVoiceLanguageKey") == std::string::npos);
    for (const ManagedSetting& setting : ManagedSettings()) {
        CHECK(setting.key.find("voice") == std::string_view::npos);
    }
}

void TestSettings(const fs::path& scratch) {
    CHECK(IsValidSetting(kSkipIntroKey, "1") && !IsValidSetting(kSkipIntroKey, "yes"));
    CHECK(IsValidSetting(kControllerIconsKey, "xbox") && !IsValidSetting(kControllerIconsKey, "nintendo"));

    // An older file with a voice_language line keeps it untouched and nothing reads it.
    const fs::path file = scratch / "features.ini";
    {
        std::ofstream out(file);
        out << "voice_language = english\nskip_intro = 1\ntext_language = german\n";
    }
    {
        LauncherSettings settings(file);
        CHECK(settings.Get("voice_language").empty());
        CHECK(settings.GetBool(kSkipIntroKey, false));
        CHECK(settings.Get(kTextLanguageKey) == "german");
        settings.Set(kTextLanguageKey, "spanish");
        CHECK(settings.SaveNow({{kControllerIconsKey, "xbox"}}));
        CHECK(settings.Save());
    }
    CHECK(Read(file).find("voice_language = english") != std::string::npos);
    LauncherSettings reloaded(file);
    CHECK(reloaded.Get(kTextLanguageKey) == "spanish");
    CHECK(reloaded.GetBool(kSkipIntroKey, false));
    CHECK(reloaded.Get(kControllerIconsKey) == "xbox");
    reloaded.ResetToDefaults();
    CHECK(!reloaded.GetBool(kSkipIntroKey, true));
    CHECK(reloaded.Get(kControllerIconsKey) == "xbox");
    CHECK(Read(file).find("voice_language = english") != std::string::npos);

    const fs::path absent = scratch / "absent.ini";
    CHECK(tsr_skip_intro(absent.string().c_str()) == 0);
    CHECK(tsr_skip_intro(file.string().c_str()) == 1);

    reloaded.Set(kTextLanguageKey, "french");
    const std::string base = SettingsSummary(reloaded, &Build("1.00"), Support(false), nullptr);
    CHECK(base.find("Text Language French") != std::string::npos);
    CHECK(base.find("Voice Language Japanese") != std::string::npos);
    CHECK(base.find("Skip intro off") != std::string::npos);
    const std::string update = SettingsSummary(reloaded, &Build("1.01"), Support(true), nullptr);
    CHECK(update.find("Voice Language game settings") != std::string::npos);
    CHECK(update.find("Text Language French") != std::string::npos);
    LanguageSupport english_only = Support(true);
    english_only.text.resize(1);
    CHECK(SettingsSummary(reloaded, &Build("1.01"), english_only, nullptr).find("Text Language English (preferred french)") !=
          std::string::npos);
}

void TestBuildStatus() {
    CHECK(std::string_view(Build("1.00").support_note).empty());
    CHECK(std::string_view(Build("1.02").support_note).empty());
    CHECK(!std::string_view(Build("1.01").support_note).empty());
    for (const GameBuild& build : SupportedBuilds()) {
        CHECK(std::string_view(build.support_note).find("Early development") == std::string_view::npos);
    }
    CHECK(ReadSource("launcher/launcher.cpp").find("Early development") == std::string::npos);
}

} // namespace

int main() {
    const fs::path scratch = fs::temp_directory_path() / "tsr-game-feature-tests";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch);
    TestEmbeddedPatches();
    TestRealArchives(scratch);
    TestInstalledPrompts();
    TestLanguageModel(scratch);
    TestNoVoiceOverride();
    TestBuildStatus();
    TestSettings(scratch);
    fs::remove_all(scratch, ec);
    std::printf("game features: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
