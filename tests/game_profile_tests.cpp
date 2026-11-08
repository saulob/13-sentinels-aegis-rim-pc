// Version handling without a window: package identities, the base and update relationship, staging, installing, switching
// and removing updates without touching the base. TSR_TEST_PACKAGES names a folder with the real USA packages, which are
// only opened.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../launcher/boot_image.h"
#include "build_diagnostics.h"
#include "controller_prompts.h"
#include "cpk_patch.h"
#include "crypto.h"
#include "game_files.h"
#include "game_install.h"
#include "pkg_extract.h"
#include "utf8.h"
#include "language_support.h"

using namespace tsr;
namespace fs = std::filesystem;

namespace {

int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

void Append32(std::string& out, uint32_t value) { out.append(reinterpret_cast<const char*>(&value), 4); }
void Append16(std::string& out, uint16_t value) { out.append(reinterpret_cast<const char*>(&value), 2); }

// A param.sfo with these string values.
std::string Sfo(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string keys, values, index;
    for (const auto& [key, value] : fields) {
        Append16(index, uint16_t(keys.size()));
        Append16(index, 0x0204);
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

constexpr char kTitle[] = "CUSA19610";
constexpr char kContent[] = "UP0177-CUSA19610_00-13SENTINELSEFIGS";

std::string PackageSfo(const char* title, const char* content, const char* category, const char* app_ver, const char* version) {
    return Sfo({{"TITLE_ID", title}, {"CONTENT_ID", content}, {"CATEGORY", category}, {"APP_VER", app_ver}, {"VERSION", version}});
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

void TestProfile() {
    const auto builds = SupportedBuilds();
    CHECK(builds.size() == 3);
    const GameBuild& base = Build("1.00");
    CHECK(&builds[0] == &base);
    CHECK(!IsUpdateBuild(base));
    CHECK(BaseBuildOf(base) == nullptr);
    for (const char* version : {"1.01", "1.02"}) {
        const GameBuild& update = Build(version);
        CHECK(IsUpdateBuild(update));
        CHECK(BaseBuildOf(update) == &base);
        CHECK(std::string(base.folder) != update.folder);
        CHECK(std::string(base.eboot_sha256) != update.eboot_sha256);
        CHECK(!RestrictedToNativeResolution(&update));
    }
    CHECK(std::string(Build("1.01").eboot_sha256) != Build("1.02").eboot_sha256);
    CHECK(!RestrictedToNativeResolution(&base));
    CHECK(RestrictedToNativeResolution(nullptr) == false);
}

void TestPackageIdentity() {
    const GameCheck base = CheckPackageIdentity(PackageSfo(kTitle, kContent, "gd", "01.00", "01.00"));
    CHECK(base.state == GameFileState::kValid);
    CHECK(base.build == &Build("1.00"));
    CHECK(base.layer == 0);
    for (const auto& [app_ver, version] : {std::pair{"01.01", "1.01"}, std::pair{"01.02", "1.02"}}) {
        const GameCheck update = CheckPackageIdentity(PackageSfo(kTitle, kContent, "gp", app_ver, "01.00"));
        CHECK(update.state == GameFileState::kValid);
        CHECK(update.build == &Build(version));
        CHECK(update.layer == 1);
        // An update must not pass as a base with the same version numbers.
        CHECK(CheckPackageIdentity(PackageSfo(kTitle, kContent, "gd", app_ver, "01.00")).state != GameFileState::kValid);
    }
    CHECK(CheckPackageIdentity(PackageSfo("CUSA19620", kContent, "gd", "01.00", "01.00")).state != GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo(kTitle, "EP0177-CUSA19620_00-13SENTINELSEFIGS", "gd", "01.00", "01.00")).state !=
          GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo(kTitle, kContent, "gp", "01.03", "01.00")).state != GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo(kTitle, kContent, "gp", "01.00", "01.00")).state != GameFileState::kValid);
}

void TestFinishedGameIdentity() {
    CHECK(CheckSfoIdentity(PackageSfo(kTitle, kContent, "gd", "01.00", "01.00")).build == &Build("1.00"));
    CHECK(CheckSfoIdentity(PackageSfo(kTitle, kContent, "gp", "01.01", "01.00")).build == &Build("1.01"));
    CHECK(CheckSfoIdentity(PackageSfo(kTitle, kContent, "gp", "01.02", "01.00")).build == &Build("1.02"));
    CHECK(CheckSfoIdentity(PackageSfo(kTitle, kContent, "gp", "01.03", "01.00")).state != GameFileState::kValid);
}

void Write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string Read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void TestStageUpdateKeepsBase(const fs::path& scratch) {
    const fs::path base = scratch / "base", update = scratch / "update", staged = scratch / "staged";
    Write(base / "eboot.bin", "base eboot");
    Write(base / "ROBO_US.CPK", "base archive");
    Write(base / "sce_sys" / "param.sfo", "base sfo");
    Write(update / "eboot.bin", "update eboot");
    Write(update / "ROBO_PATCH_US.CPK", "patch archive");
    Write(update / "sce_sys" / "param.sfo", "update sfo");

    const InstallResult result = StageUpdate(base, update, staged);
    CHECK(result.ok());
    CHECK(Read(staged / "eboot.bin") == "update eboot");
    CHECK(Read(staged / "ROBO_PATCH_US.CPK") == "patch archive");
    CHECK(Read(staged / "ROBO_US.CPK") == "base archive");
    CHECK(Read(staged / "sce_sys" / "param.sfo") == "update sfo");
    // The staged files are links into the base: a replaced one must not have written through.
    CHECK(Read(base / "eboot.bin") == "base eboot");
    CHECK(Read(base / "sce_sys" / "param.sfo") == "base sfo");
    CHECK(!fs::exists(base / "ROBO_PATCH_US.CPK"));
}

void TestLanguages(const fs::path& scratch) {
    const fs::path app0 = scratch / "languages";
    const GameBuild* base = &Build("1.00");
    Write(app0 / "ROBO_US.CPK", "x");
    Write(app0 / "ROBO_FR.CPK", "x");
    const std::vector<TextLanguage> installed = InstalledTextLanguages(app0, base);
    CHECK(installed.size() == 2);
    CHECK(installed[0].key == "english");
    CHECK(installed[1].key == "french");
    CHECK(InstalledTextLanguages(scratch / "absent", base).size() == base->text_languages.size());
}

void TestMessages() {
    CHECK(RefusedPackageKind(InstallStatus::kExpectedBasePackage));
    CHECK(RefusedPackageKind(InstallStatus::kExpectedUpdatePackage));
    CHECK(!RefusedPackageKind(InstallStatus::kInstalled));
    CHECK(DescribeInstall(InstallStatus::kExpectedBasePackage).title != DescribeInstall(InstallStatus::kExpectedUpdatePackage).title);
    CHECK(DescribeInstall(InstallStatus::kUpdateNeedsBase).text.find("CUSA19610") != std::string::npos);
}

// scripts/game_profiles.py names the same builds and executables.
void TestScriptsAgree() {
    const std::string script = Read(fs::path(TSR_SOURCE_DIR) / "scripts" / "game_profiles.py");
    CHECK(!script.empty());
    for (const GameBuild& build : SupportedBuilds()) {
        CHECK(script.find(build.eboot_sha256) != std::string::npos);
        CHECK(script.find(build.content_id) != std::string::npos);
        CHECK(script.find(std::string("'") + build.folder + "'") != std::string::npos);
        for (const char* file : build.extra_files) {
            CHECK(script.find(std::string("'") + file + "'") != std::string::npos);
        }
    }
}

std::string HexSha256(const std::vector<uint8_t>& bytes) {
    uint8_t digest[32];
    if (!crypto::Sha256(bytes.data(), bytes.size(), digest)) {
        return {};
    }
    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    for (const uint8_t b : digest) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
    }
    return hex;
}

// The table of contents of an archive inside a package, read without unpacking it.
bool PackageArchive(Package& package, const std::string& name, CpkArchive& archive) {
    std::vector<uint8_t> head;
    if (!package.ReadFileRange(name, 0, 1u << 20, head).ok()) {
        return false;
    }
    std::istringstream in(std::string(head.begin(), head.end()));
    return archive.OpenStream(in);
}

bool HasFile(const std::vector<PackageFile>& files, const std::string& path) {
    for (const PackageFile& file : files) {
        if (file.path == path) {
            return true;
        }
    }
    return false;
}

struct RealPackages {
    fs::path base, update101, update102;
    bool complete() const { return !base.empty() && !update101.empty() && !update102.empty(); }
};

// The USA packages in TSR_TEST_PACKAGES, told apart by their param.sfo, never by file name.
RealPackages FindRealPackages(const char* folder) {
    RealPackages found;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
        if (entry.path().extension() != ".pkg") {
            continue;
        }
        Package package;
        if (!package.Open(entry.path()).ok() || SfoString(package.sfo(), "TITLE_ID") != "CUSA19610") {
            continue;
        }
        const std::string app_ver = SfoString(package.sfo(), "APP_VER");
        (app_ver == "01.00" ? found.base : app_ver == "01.01" ? found.update101 : found.update102) = entry.path();
    }
    return found;
}

// What a real update package holds, read in place: identity, executable fingerprint, archives, modules, English bank.
void TestRealUpdateContents(const fs::path& file, const GameBuild& update) {
    Package package;
    CHECK(package.Open(file).ok());
    CHECK(package.LoadFiles().ok());
    const std::string& sfo = package.sfo();
    CHECK(SfoString(sfo, "TITLE_ID") == "CUSA19610");
    CHECK(SfoString(sfo, "CONTENT_ID") == update.content_id);
    CHECK(SfoString(sfo, "CATEGORY") == "gp");
    CHECK(SfoString(sfo, "APP_VER") == FinishedLayer(update).app_ver);
    CHECK(SfoString(sfo, "VERSION") == "01.00");
    const std::vector<PackageFile> files = package.Files();
    for (const char* name : update.extra_files) {
        const bool from_base = std::string_view(name).starts_with("ROBO_") && !std::string_view(name).starts_with("ROBO_PATCH");
        CHECK(from_base != HasFile(files, name));
    }
    const bool own_modules = std::string_view(update.version) == "1.02";
    for (const char* module : kBundledModules) {
        CHECK(HasFile(files, std::string("sce_module/") + module) == own_modules);
    }
    CHECK(HasFile(files, "ROBO_PATCH2_US.CPK") == own_modules);
    std::vector<uint8_t> eboot;
    CHECK(package.ReadFile("eboot.bin", 64u << 20, eboot).ok());
    CHECK(HexSha256(eboot) == update.eboot_sha256);
    CpkArchive patch;
    CHECK(PackageArchive(package, kEnglishVoices.archive, patch));
    CHECK(patch.Find(kEnglishVoices.cues) && patch.Find(kEnglishVoices.stream));
    CHECK(!patch.Find(kJapaneseVoices.cues));
    std::printf("package %s: identity, executable, archives, modules and English bank verified\n", update.label);
}

void TestRealBaseContents(const fs::path& file) {
    const GameBuild& base = Build("1.00");
    Package package;
    CHECK(package.Open(file).ok());
    CHECK(package.LoadFiles().ok());
    CHECK(SfoString(package.sfo(), "CATEGORY") == "gd");
    CHECK(SfoString(package.sfo(), "APP_VER") == "01.00");
    CHECK(!HasFile(package.Files(), kEnglishVoices.archive));
    std::vector<uint8_t> eboot;
    CHECK(package.ReadFile("eboot.bin", 64u << 20, eboot).ok());
    CHECK(HexSha256(eboot) == base.eboot_sha256);
    CpkArchive main;
    CHECK(PackageArchive(package, kJapaneseVoices.archive, main));
    CHECK(main.Find(kJapaneseVoices.cues) && main.Find(kJapaneseVoices.stream));
    CHECK(!main.Find(kEnglishVoices.cues));
    std::printf("package %s: Japanese bank only\n", base.label);
}

fs::path ArtFolder() {
    return fs::path(TSR_SOURCE_DIR) / "build" / "test-prompt-art";
}

std::string FlatName(std::string_view entry) {
    std::string name(entry);
    std::replace(name.begin(), name.end(), '/', '_');
    return name;
}

bool WriteEntry(const fs::path& archive_file, const std::string& entry, const fs::path& out) {
    CpkArchive archive;
    const CpkEntry* found = archive.Open(archive_file) ? archive.Find(entry) : nullptr;
    if (!found) {
        return false;
    }
    const std::vector<uint8_t> data = archive.Read(*found);
    fs::create_directories(out.parent_path());
    std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return !data.empty();
}

bool WritePackageFile(Package& package, const std::string& name, uint64_t limit, const fs::path& out) {
    std::vector<uint8_t> bytes;
    if (!package.ReadFile(name, limit, bytes).ok()) {
        return false;
    }
    fs::create_directories(out.parent_path());
    std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return true;
}

// One real archive from a package, before and after the Xbox patch, for tests/prompt_art_tests.py (which looks at the art).
void ExportArt(Package& package, const char* archive_name, const GameBuild& version, const std::vector<std::string>& extra,
               const std::string& folder) {
    const fs::path root = ArtFolder() / folder;
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path before = root / "original" / "files", after = root / "patched" / "files";
    fs::create_directories(before);
    CHECK(WritePackageFile(package, archive_name, 400u << 20, before / archive_name));
    fs::copy_file(before / archive_name, after / archive_name, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        fs::create_directories(after);
        fs::copy_file(before / archive_name, after / archive_name, fs::copy_options::overwrite_existing, ec);
    }
    const char* names[] = {archive_name};
    GameBuild single = version;
    single.prompt_archives = names;
    const PromptsResult patched = SetPromptStyle(after, single, PromptStyle::kXbox, XboxPromptsPatch(version));
    CHECK(patched.ok);
    const std::optional<BlockPatch> patch = ParseBlockPatch(XboxPromptsPatch(version));
    CHECK(patch.has_value());
    if (!patch || !patched.ok) {
        return;
    }
    CHECK(InstalledPromptState(after, single, XboxPromptsPatch(version)) == PromptState::kXbox);
    std::vector<std::string> entries = extra;
    for (const BlockPatchTarget& target : PatchForArchive(*patch, archive_name).targets) {
        entries.push_back(target.name);
    }
    for (const std::string& entry : entries) {
        CHECK(WriteEntry(before / archive_name, entry, root / "original" / FlatName(entry)));
        CHECK(WriteEntry(after / archive_name, entry, root / "patched" / FlatName(entry)));
    }
    CHECK(SetPromptStyle(after, single, PromptStyle::kPlayStation, XboxPromptsPatch(version)).ok);
    CHECK(Read(after / archive_name) == Read(before / archive_name));
    CHECK(InstalledPromptState(after, single, XboxPromptsPatch(version)) == PromptState::kPlayStation);
    fs::remove_all(root / "original" / "files", ec);
    fs::remove_all(root / "patched" / "files", ec);
}

// The English manual pages the icon templates are cut from (compressed in the 3.7 GB archive; only they are read).
void ExportTemplatePages(Package& package, const std::string& folder) {
    CpkArchive archive;
    CHECK(PackageArchive(package, kJapaneseVoices.archive, archive));
    for (const char* page : {"Manual/Manual05.ftx", "Manual/Manual06.ftx"}) {
        const CpkEntry* entry = archive.Find(page);
        CHECK(entry != nullptr);
        std::vector<uint8_t> bytes;
        if (!entry || !package.ReadFileRange(kJapaneseVoices.archive, entry->offset, entry->size, bytes).ok()) {
            CHECK(false);
            continue;
        }
        if (entry->compressed()) {
            bytes = CrilaylaDecompress(bytes);
        }
        const fs::path out = ArtFolder() / folder / FlatName(page);
        fs::create_directories(out.parent_path());
        std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        CHECK(Crc32(bytes) != 0 && bytes.size() == 2089424);
    }
}

void ExportPromptArt(const fs::path& base_package, const fs::path& update_package) {
    Package base, update;
    if (!base.Open(base_package).ok() || !base.LoadFiles().ok() || !update.Open(update_package).ok() ||
        !update.LoadFiles().ok()) {
        CHECK(false);
        return;
    }
    ExportArt(base, "ROBO_FR.CPK", Build("1.00"), {"GUI/Interface.mbs"}, "100");
    ExportArt(update, "ROBO_PATCH2_FR.CPK", Build("1.02"), {"gui/interface.mbs"}, "102");
    ExportTemplatePages(base, "templates");
    std::printf("prompt art: ROBO_FR.CPK of 1.00 and ROBO_PATCH2_FR.CPK of 1.02 exported before and after the patch to "
                "build\\test-prompt-art\n");
}

// The real packages: each refused in the other's action before anything is unpacked; other regions refused.
void TestRealPackages(const fs::path& scratch) {
    const char* folder = std::getenv("TSR_TEST_PACKAGES");
    if (!folder || !*folder) {
        std::printf("skipped: TSR_TEST_PACKAGES is not set\n");
        return;
    }
    const RealPackages packages = FindRealPackages(folder);
    CHECK(packages.complete());
    const GameRoots roots{scratch / "prepared", scratch / "image"};
    std::error_code ec;
    int foreign = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
        const fs::path& file = entry.path();
        if (file.extension() != ".pkg") {
            continue;
        }
        const auto time_before = fs::last_write_time(file);
        const auto size_before = fs::file_size(file);
        const bool base = file == packages.base;
        const bool update = file == packages.update101 || file == packages.update102;
        InstallHooks hooks;
        hooks.expected = update ? PackageKind::kBase : PackageKind::kUpdate;
        const InstallResult result = InstallFromPackage(file, roots, hooks);
        if (base || update) {
            CHECK(result.status == (update ? InstallStatus::kExpectedBasePackage : InstallStatus::kExpectedUpdatePackage));
        } else {
            CHECK(!result.ok());
            ++foreign;
        }
        CHECK(fs::last_write_time(file) == time_before);
        CHECK(fs::file_size(file) == size_before);
        CHECK(!fs::exists(roots.prepared / "CUSA19610-1.00" / "eboot.bin"));
    }
    std::printf("packages: base and both updates refused in the other action, %d other-region package(s) refused; all "
                "untouched\n", foreign);
    if (packages.complete()) {
        TestRealBaseContents(packages.base);
        TestRealUpdateContents(packages.update101, Build("1.01"));
        TestRealUpdateContents(packages.update102, Build("1.02"));
        ExportPromptArt(packages.base, packages.update102);
    }
}

// The voice table an update's executable reads (Other/ScriptText.bin from the patch archive it binds last): a Japanese
// and an English cue per line, where the base's table has the Japanese one only. Both list the same lines.
void TestVoiceTable(const fs::path& base_app0, const fs::path& table_archive) {
    const auto table = [](const fs::path& archive_file) {
        CpkArchive archive;
        std::vector<uint8_t> data;
        if (archive.Open(archive_file)) {
            if (const CpkEntry* entry = archive.Find("Other/ScriptText.bin")) {
                data = archive.Read(*entry);
            }
        }
        return data;
    };
    const std::vector<uint8_t> japanese = table(base_app0 / "ROBO_US.CPK");
    const std::vector<uint8_t> both = table(table_archive);
    CHECK(!japanese.empty() && japanese.size() % 12 == 0);
    CHECK(!both.empty() && both.size() % 16 == 0 && both.size() / 16 == japanese.size() / 12);
    if (japanese.empty() || both.empty() || both.size() / 16 != japanese.size() / 12) {
        return;
    }
    size_t english = 0, same_japanese = 0;
    for (size_t row = 0; row < both.size() / 16; ++row) {
        int32_t jp_old, jp_new, en;
        std::memcpy(&jp_old, japanese.data() + row * 12 + 4, 4);
        std::memcpy(&jp_new, both.data() + row * 16 + 4, 4);
        std::memcpy(&en, both.data() + row * 16 + 8, 4);
        same_japanese += jp_old == jp_new;
        english += en >= 200000;
    }
    CHECK(same_japanese == both.size() / 16);
    CHECK(english > 20000);
    std::printf("voice table %s: %zu lines, %zu with an English cue\n", Utf8(table_archive.filename()).c_str(),
                both.size() / 16, english);
}

using FileSnapshot = std::map<std::string, std::pair<uintmax_t, fs::file_time_type>>;

FileSnapshot Snapshot(const fs::path& dir) {
    FileSnapshot files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            files[it->path().lexically_relative(dir).generic_string()] = {it->file_size(ec), it->last_write_time(ec)};
        }
    }
    return files;
}

InstallResult Install(const fs::path& package, const GameRoots& roots, PackageKind kind) {
    InstallHooks hooks;
    hooks.expected = kind;
    const InstallResult result = InstallFromPackage(package, roots, hooks);
    std::printf("install %s: %s (%s)\n", Utf8(package.filename()).c_str(), DescribeInstall(result.status).title.c_str(),
                result.detail.c_str());
    return result;
}

void CheckUpdateInstalled(const GameRoots& roots, const GameBuild& update) {
    const GamePaths base = BuildPaths(roots, Build("1.00"));
    const GamePaths paths = BuildPaths(roots, update);
    const GameCheck check = CheckGameFiles(paths);
    CHECK(check.state == GameFileState::kValid && check.build == &update);
    CHECK(Sha256Hex(paths.app0 / "eboot.bin") == update.eboot_sha256);
    std::error_code ec;
    CHECK(!fs::equivalent(base.app0 / "eboot.bin", paths.app0 / "eboot.bin", ec));
    for (const char* file : update.extra_files) {
        CHECK(fs::exists(paths.app0 / file));
    }
    CHECK(EnglishVoicesAvailable(paths.app0, &update));
    const std::optional<BlockPatch> icons = ParseBlockPatch(XboxPromptsPatch(update));
    CHECK(icons && PromptPatchFits(paths.app0, update, *icons));
    const fs::path work_image =
        fs::path(TSR_SOURCE_DIR) / "game" / "work" / ("image-" + std::string(update.version)) / "boot.bin";
    if (fs::exists(work_image, ec)) {
        CHECK(Read(paths.image_dir / "boot.bin") == Read(work_image));
        std::printf("%s boot image: identical to game\\work\\image-%s\n", update.label, update.version);
    }
}

// The whole workflow on the real packages, into TSR_TEST_INSTALL (disposable; about 5 GB, removed again): base, either
// update alone, both, switching, removing each and the active one, reinstalling. The packages are only read.
void TestRealInstall() {
    const char* folder = std::getenv("TSR_TEST_PACKAGES");
    const char* target = std::getenv("TSR_TEST_INSTALL");
    if (!folder || !*folder || !target || !*target) {
        std::printf("skipped: TSR_TEST_PACKAGES and TSR_TEST_INSTALL are not both set\n");
        return;
    }
    const RealPackages packages = FindRealPackages(folder);
    CHECK(packages.complete());
    if (!packages.complete()) {
        return;
    }
    const fs::path root = target;
    std::error_code ec;
    fs::remove_all(root, ec);
    const GameRoots roots{root / "prepared", root / "image"};
    const GameBuild& base = Build("1.00");
    const GameBuild& first = Build("1.01");
    const GameBuild& second = Build("1.02");
    const fs::path user = root / "user";
    Write(user / "savedata" / "1" / "CUSA19610" / "SAVEDATA00" / "ROBO13.DAT", "save");
    Write(user / "trophy" / "NPWR16758_00" / "trophy.dat", "trophies");
    Write(user / "tsr.ini", "voice_language = english\n");
    const FileSnapshot user_before = Snapshot(user);

    const InstallResult needs_base = Install(packages.update101, roots, PackageKind::kUpdate);
    CHECK(needs_base.status == InstallStatus::kUpdateNeedsBase);
    CHECK(needs_base.detail.find("the base CUSA19610 1.00 USA is not installed") != std::string::npos);
    CHECK(needs_base.recognition.find("is CUSA19610 1.01 USA (update, layer 2 of 2, laid over CUSA19610 1.00 USA)") !=
          std::string::npos);
    const InstallResult base_install = Install(packages.base, roots, PackageKind::kBase);
    CHECK(base_install.status == InstallStatus::kInstalled);
    CHECK(base_install.recognition.find("is CUSA19610 1.00 USA (base, layer 1 of 1)") != std::string::npos);
    const fs::path base_app0 = BuildPaths(roots, base).app0;
    CHECK(Sha256Hex(base_app0 / "eboot.bin") == base.eboot_sha256);
    CHECK(!EnglishVoicesAvailable(base_app0, &base));
    const FileSnapshot base_before = Snapshot(base_app0);

    InstallHooks interrupted;
    interrupted.expected = PackageKind::kUpdate;
    interrupted.progress = [](uint64_t done, uint64_t) { return done < (64u << 20); };
    CHECK(InstallFromPackage(packages.update102, roots, interrupted).status == InstallStatus::kCancelled);
    CHECK(CheckGameFiles(BuildPaths(roots, second)).state != GameFileState::kValid);
    CHECK(LastPreparedBuild(roots) == &base);

    // 1.02 straight over the base, then 1.01 beside it: the higher version stays the effective one (no silent
    // downgrade) even though 1.01 was installed more recently.
    CHECK(Install(packages.update102, roots, PackageKind::kUpdate).status == InstallStatus::kInstalled);
    CHECK(LastPreparedBuild(roots) == &second);
    CheckUpdateInstalled(roots, second);
    CHECK(Install(packages.update101, roots, PackageKind::kUpdate).status == InstallStatus::kInstalled);
    CHECK(LastPreparedBuild(roots) == &second);
    CheckUpdateInstalled(roots, first);
    CHECK(PreparedBuilds(roots).size() == 3);
    const fs::path first_app0 = BuildPaths(roots, first).app0;
    const fs::path second_app0 = BuildPaths(roots, second).app0;
    CHECK(fs::equivalent(base_app0 / "ROBO_US.CPK", first_app0 / "ROBO_US.CPK", ec));
    CHECK(fs::equivalent(base_app0 / "ROBO_US.CPK", second_app0 / "ROBO_US.CPK", ec));
    CHECK(!fs::exists(first_app0 / "ROBO_PATCH2_US.CPK"));
    TestVoiceTable(base_app0, first_app0 / "ROBO_PATCH_US.CPK");
    TestVoiceTable(base_app0, second_app0 / "ROBO_PATCH2_US.CPK");

    // There is no version switch any more: every installed build stays individually valid, but only the highest one
    // (1.02) is effective, whichever of them was looked at or reinstalled last.
    for (const GameBuild* build : {&base, &second, &first, &base, &first}) {
        CHECK(ActivateInstalledBuild(roots, *build));
        CHECK(LastPreparedBuild(roots) == &second);
        CHECK(CheckGameFiles(BuildPaths(roots, *build)).build == build);
    }
    CHECK(Install(packages.update101, roots, PackageKind::kUpdate).status == InstallStatus::kUpToDate);
    CHECK(LastPreparedBuild(roots) == &second);

    // 1.02 is effective: removing it falls back to 1.01, still installed, not straight to the base.
    const RemoveResult removed_second = RemoveInstalledUpdate(roots, second);
    CHECK(removed_second.ok() && !removed_second.base_selected);
    CHECK(!fs::exists(second_app0) && !fs::exists(BuildPaths(roots, second).image_dir));
    CHECK(LastPreparedBuild(roots) == &first);
    CheckUpdateInstalled(roots, first);
    const RemoveResult removed_first = RemoveInstalledUpdate(roots, first);
    CHECK(removed_first.ok() && removed_first.base_selected);
    CHECK(LastPreparedBuild(roots) == &base);
    CHECK(PreparedBuilds(roots).size() == 1);
    CHECK(RemoveInstalledUpdate(roots, first).status == RemoveStatus::kNotInstalled);
    CHECK(RemoveInstalledUpdate(roots, base).status == RemoveStatus::kNotUpdate);

    // Reinstalled after removal (1.02 directly over the base again, then 1.01 beside it): 1.02 stays effective.
    CHECK(Install(packages.update102, roots, PackageKind::kUpdate).status == InstallStatus::kInstalled);
    CHECK(Install(packages.update101, roots, PackageKind::kUpdate).status == InstallStatus::kInstalled);
    CheckUpdateInstalled(roots, second);
    CHECK(LastPreparedBuild(roots) == &second);
    // Removing the effective (highest) update while a lower one remains falls back to it, not to the base.
    const RemoveResult removed_active = RemoveInstalledUpdate(roots, second);
    CHECK(removed_active.ok() && !removed_active.base_selected);
    CHECK(LastPreparedBuild(roots) == &first);
    CHECK(CheckGameFiles(BuildPaths(roots, first)).build == &first);
    CHECK(ActivateInstalledBuild(roots, first));

    // None of this ever changes the base: no install, removal or validity check of another build touches its
    // files, not even its preparation marker's timestamp.
    CHECK(Snapshot(base_app0) == base_before);
    CHECK(Sha256Hex(base_app0 / "eboot.bin") == base.eboot_sha256);
    CHECK(CheckGameFiles(BuildPaths(roots, base)).build == &base);
    CHECK(Snapshot(user) == user_before);
    if (!failures) {
        std::printf("real install: base, interrupted update, 1.02 over base, 1.01 beside it (no downgrade), removals "
                    "fall back by precedence, reinstall; base files and user files untouched\n");
    }
    fs::remove_all(root, ec);
}

// Removal on a disposable synthetic install: what it refuses and what it must not delete.
void TestRemoveUpdateSafety(const fs::path& scratch) {
    const fs::path root = scratch / "remove";
    const GameRoots roots{root / "prepared", root / "image"};
    const GameBuild& base = Build("1.00");
    const GameBuild& first = Build("1.01");
    const GameBuild& second = Build("1.02");
    const auto make = [&](const GameBuild& build, const std::string& sfo) {
        const GamePaths paths = BuildPaths(roots, build);
        Write(paths.app0 / "sce_sys" / "param.sfo", sfo);
        Write(paths.app0 / kPreparedMarker, "{}");
        Write(paths.image_dir / "boot.bin", "image");
        return paths;
    };
    const GamePaths base_paths = make(base, PackageSfo(kTitle, kContent, "gd", "01.00", "01.00"));
    Write(base_paths.app0 / "ROBO_US.CPK", "base archive");
    const std::string first_sfo = PackageSfo(kTitle, kContent, "gp", "01.01", "01.00");
    const GamePaths first_paths = make(first, first_sfo);
    std::error_code ec;
    fs::create_hard_link(base_paths.app0 / "ROBO_US.CPK", first_paths.app0 / "ROBO_US.CPK", ec);
    const bool linked = !ec;
    const GamePaths foreign = make(second, first_sfo);
    Write(root / "user" / "savedata" / "ROBO13.DAT", "save");

    CHECK(RemoveInstalledUpdate(roots, base).status == RemoveStatus::kNotUpdate);
    CHECK(fs::exists(base_paths.app0 / "ROBO_US.CPK"));
    CHECK(RemoveInstalledUpdate(roots, second).status == RemoveStatus::kForeignFolder);
    CHECK(fs::exists(foreign.app0 / "sce_sys" / "param.sfo"));

#ifdef _WIN32
    {
        const fs::path busy = first_paths.app0 / "eboot.bin";
        Write(busy, "busy");
        HANDLE handle = CreateFileW(busy.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(handle != INVALID_HANDLE_VALUE);
        CHECK(RemoveInstalledUpdate(roots, first).status == RemoveStatus::kInUse);
        CHECK(fs::exists(first_paths.app0 / "sce_sys" / "param.sfo") && fs::exists(first_paths.image_dir / "boot.bin"));
        CloseHandle(handle);
    }
#endif
    const RemoveResult removed = RemoveInstalledUpdate(roots, first);
    CHECK(removed.ok());
    CHECK(!fs::exists(first_paths.app0) && !fs::exists(first_paths.image_dir));
    CHECK(Read(base_paths.app0 / "ROBO_US.CPK") == "base archive");
    CHECK(fs::exists(base_paths.image_dir / "boot.bin"));
    CHECK(fs::exists(foreign.app0 / "sce_sys" / "param.sfo"));
    CHECK(Read(root / "user" / "savedata" / "ROBO13.DAT") == "save");
    CHECK(RemoveInstalledUpdate(roots, first).status == RemoveStatus::kNotInstalled);
    if (!linked) {
        std::printf("skipped: hard links unavailable in %s\n", Utf8(scratch).c_str());
    }

    // What an interrupted removal left aside is finished later and never counts as installed.
    const fs::path aside = roots.prepared / (std::string(first.folder) + ".removing");
    Write(aside / "sce_sys" / "param.sfo", first_sfo);
    CHECK(FinishInterruptedRemovals(roots));
    CHECK(!fs::exists(aside));

    // A root that is a link elsewhere is refused.
    const fs::path elsewhere = root / "elsewhere";
    const GameRoots linked_roots{root / "linked-prepared", roots.image};
    Write(elsewhere / first.folder / "sce_sys" / "param.sfo", first_sfo);
    fs::create_directory_symlink(elsewhere, linked_roots.prepared, ec);
    if (!ec) {
        CHECK(RemoveInstalledUpdate(linked_roots, first).status == RemoveStatus::kForeignFolder);
        CHECK(fs::exists(elsewhere / first.folder / "sce_sys" / "param.sfo"));
    } else {
        std::printf("skipped: directory symbolic links need Developer Mode or rights here (link-root refusal)\n");
    }
    std::printf("removal (synthetic): base, foreign and busy folders refused, hard-linked base data and saves kept, "
                "leftovers finished\n");
    fs::remove_all(root, ec);
}

// The local install (game\prepared and build\image), only read: valid as is, rebuilt byte for byte in scratch, and refused
// with a wrong content profile signature.
void TestLocalInstall(const fs::path& scratch) {
    const GameBuild& base = Build("1.00");
    const fs::path repo = TSR_SOURCE_DIR;
    const GamePaths local{repo / "game" / "prepared" / base.folder, repo / "build" / "image" / base.folder};
    std::error_code ec;
    if (!fs::exists(local.app0 / "eboot.bin", ec) || !fs::exists(local.image_dir / "boot.bin", ec)) {
        std::printf("skipped: no local install of %s %s (game\\prepared and build\\image)\n", base.title_id, base.version);
        return;
    }
    const GameCheck check = CheckGameFiles(local);
    CHECK(check.state == GameFileState::kValid);
    CHECK(check.build == &base);
    CHECK(check.eboot_sha256 == base.eboot_sha256);

    const fs::path rebuilt = scratch / "rebuilt-image";
    CHECK(BuildBootImage(local.app0, rebuilt).empty());
    CHECK(Read(rebuilt / "boot.bin") == Read(local.image_dir / "boot.bin"));
    CHECK(Read(rebuilt / "content.bin") == Read(local.image_dir / "content.bin"));
    CHECK(Read(rebuilt / "boot.bin").compare(0, kBootImageMagic.size(), kBootImageMagic) == 0);
    CHECK(CheckGameFiles({local.app0, rebuilt}).state == GameFileState::kValid);

    std::string content = Read(rebuilt / "content.bin");
    content.replace(0, 3, "DCP");
    {
        std::ofstream out(rebuilt / "content.bin", std::ios::binary | std::ios::trunc);
        out << content;
    }
    const GameCheck stale = CheckGameFiles({local.app0, rebuilt});
    CHECK(stale.state == GameFileState::kMissing);
    CHECK(stale.detail.find("outdated format") != std::string::npos);
    std::printf("local install %s %s: valid, boot image reproduced, wrong signature refused\n", base.title_id, base.version);
}

bool AnyLine(const std::vector<std::string>& lines, std::string_view text) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

// What an unsupported or mismatched build is reported as. The installed game's executables are only read; every other
// file here is a stand-in that only has to exist.
void TestBuildDiagnostics(const fs::path& scratch) {
    const std::string unknown_update = PackageSfo(kTitle, kContent, "gp", "01.03", "01.00");
    const fs::path odd = scratch / "diagnostics-odd";
    Write(odd / "sce_sys" / "param.sfo", unknown_update);
    Write(odd / "eboot.bin", std::string(4096, 'x'));
    const UnsupportedReport report = DiagnoseUnsupportedBuild(*OpenFolderSource(odd), unknown_update);
    CHECK(report.recognized && !report.lines.empty() && report.lines.front() == kDiagnosticsBegin &&
          report.lines.back() == kDiagnosticsEnd);
    CHECK(AnyLine(report.lines, "this is an update package or folder (CATEGORY gp)"));
    CHECK(AnyLine(report.lines, "supported build of this TITLE_ID: CUSA19610 1.00 USA (base game, APP_VER 01.00"));
    CHECK(AnyLine(report.lines, "supported build of this TITLE_ID: CUSA19610 1.02 USA (base game with update, APP_VER 01.02"));
    CHECK(AnyLine(report.lines, "identity check: param.sfo APP_VER 01.03 / VERSION 01.00 is not part of a supported build"));
    CHECK(AnyLine(report.lines, "eboot.bin: 4096 bytes, SHA-256 "));
    CHECK(AnyLine(report.lines, "eboot.bin matches no supported executable (CUSA19610 1.00 USA, CUSA19610 1.01 USA and CUSA19610 1.02 USA)"));
    CHECK(AnyLine(report.lines, "Skip Intro, Exit Game and Render resolution are tied to a supported build's exact code and stay off"));
    CHECK(!DiagnoseUnsupportedBuild(*OpenFolderSource(odd), PackageSfo("CUSA00000", "XX0000-CUSA00000_00-OTHERGAME", "gd", "01.00", "01.00")).recognized);

    const fs::path prepared = fs::path(TSR_SOURCE_DIR) / "game" / "prepared";
    const fs::path real100 = prepared / Build("1.00").folder, real102 = prepared / Build("1.02").folder;
    std::error_code ec;
    if (!fs::is_regular_file(real100 / "eboot.bin", ec) || !fs::is_regular_file(real102 / "eboot.bin", ec)) {
        std::printf("skipped: no installed USA 1.00 and 1.02 (game\\prepared): executable recognition checks\n");
        return;
    }
    const std::string sfo100 = PackageSfo(kTitle, kContent, "gd", "01.00", "01.00");
    const std::string sfo102 = PackageSfo(kTitle, kContent, "gp", "01.02", "01.00");
    const UnsupportedReport matching = DiagnoseUnsupportedBuild(*OpenFolderSource(real100), sfo100);
    CHECK(AnyLine(matching.lines, "eboot.bin is the executable of CUSA19610 1.00 USA") && !AnyLine(matching.lines, "stale"));
    const UnsupportedReport stale = DiagnoseUnsupportedBuild(*OpenFolderSource(real100), sfo102);
    CHECK(AnyLine(stale.lines, "eboot.bin is the executable of CUSA19610 1.00 USA, but param.sfo names CUSA19610 1.02 USA: "
                               "the executable or the update is stale or mismatched"));
    const UnsupportedReport unnamed = DiagnoseUnsupportedBuild(*OpenFolderSource(real102), unknown_update);
    CHECK(AnyLine(unnamed.lines, "eboot.bin is the executable of CUSA19610 1.02 USA, but param.sfo names no supported build"));

    // The game folder check says the same: another supported build's executable means mixed files.
    const GameBuild& named = Build("1.02");
    const fs::path mixed = scratch / "diagnostics-mixed";
    Write(mixed / "sce_sys" / "param.sfo", sfo102);
    for (const char* name : kSharedGameFiles) {
        if (std::string_view(name) != "eboot.bin" && std::string_view(name) != "sce_sys/param.sfo") {
            Write(mixed / name, "x");
        }
    }
    Write(mixed / named.regional_archive, "x");
    for (const char* name : named.extra_files) {
        Write(mixed / name, "x");
    }
    fs::copy_file(real100 / "eboot.bin", mixed / "eboot.bin", fs::copy_options::overwrite_existing, ec);
    const GameCheck mixed_check = CheckGameTree(mixed);
    CHECK(mixed_check.state == GameFileState::kDifferentVersion && mixed_check.build == &named);
    CHECK(mixed_check.detail.find(std::string("eboot.bin is not the supported build: SHA-256 ") + Build("1.00").eboot_sha256 +
                                  ", param.sfo names CUSA19610 1.02 USA") != std::string::npos);
    CHECK(mixed_check.detail.find("it is the executable of CUSA19610 1.00 USA, so the files mix builds") != std::string::npos);
    Write(mixed / "eboot.bin", "not an executable");
    const GameCheck foreign_check = CheckGameTree(mixed);
    CHECK(foreign_check.state == GameFileState::kDifferentVersion && foreign_check.detail.find("SHA-256 ") != std::string::npos);
    CHECK(foreign_check.detail.find("mix builds") == std::string::npos);
    std::printf("build diagnostics: unsupported executable, stale executable, mixed files and unknown update reported\n");
}

} // namespace

int main() {
    const fs::path scratch = fs::temp_directory_path() / "tsr-game-profile-tests";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch);
    TestProfile();
    TestPackageIdentity();
    TestFinishedGameIdentity();
    TestStageUpdateKeepsBase(scratch);
    TestLanguages(scratch);
    TestMessages();
    TestScriptsAgree();
    TestBuildDiagnostics(scratch);
    TestRealPackages(scratch);
    TestRealInstall();
    TestRemoveUpdateSafety(scratch);
    TestLocalInstall(scratch);
    fs::remove_all(scratch, ec);
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
