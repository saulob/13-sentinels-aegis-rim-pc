// Version handling without a window: package identities, the base and update relationship, and that staging an update
// leaves the base untouched. TSR_TEST_PACKAGES names a folder with the real USA packages, which are only opened.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "boot_image.h"
#include "game_files.h"
#include "game_install.h"

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

void TestProfile() {
    const auto builds = SupportedBuilds();
    CHECK(builds.size() == 2);
    const GameBuild& base = builds[0];
    const GameBuild& update = builds[1];
    CHECK(std::string(base.version) == "1.00");
    CHECK(std::string(update.version) == "1.02");
    CHECK(!IsUpdateBuild(base));
    CHECK(IsUpdateBuild(update));
    CHECK(BaseBuildOf(update) == &base);
    CHECK(BaseBuildOf(base) == nullptr);
    CHECK(std::string(base.folder) != update.folder);
    CHECK(std::string(base.eboot_sha256) != update.eboot_sha256);
    // No render size site has been verified, so each build runs at its native size.
    CHECK(RestrictedToNativeResolution(&base));
    CHECK(RestrictedToNativeResolution(&update));
}

void TestPackageIdentity() {
    const GameCheck base = CheckPackageIdentity(PackageSfo(kTitle, kContent, "gd", "01.00", "01.00"));
    CHECK(base.state == GameFileState::kValid);
    CHECK(base.build == &SupportedBuilds()[0]);
    CHECK(base.layer == 0);

    const GameCheck update = CheckPackageIdentity(PackageSfo(kTitle, kContent, "gp", "01.02", "01.00"));
    CHECK(update.state == GameFileState::kValid);
    CHECK(update.build == &SupportedBuilds()[1]);
    CHECK(update.layer == 1);

    CHECK(CheckPackageIdentity(PackageSfo("CUSA19620", kContent, "gd", "01.00", "01.00")).state != GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo(kTitle, "EP0177-CUSA19620_00-13SENTINELSEFIGS", "gd", "01.00", "01.00")).state !=
          GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo(kTitle, kContent, "gp", "01.03", "01.00")).state != GameFileState::kValid);
    // An update must not pass as a base with the same version numbers, nor the reverse.
    CHECK(CheckPackageIdentity(PackageSfo(kTitle, kContent, "gp", "01.00", "01.00")).state != GameFileState::kValid);
    CHECK(CheckPackageIdentity(PackageSfo(kTitle, kContent, "gd", "01.02", "01.00")).state != GameFileState::kValid);
}

void TestFinishedGameIdentity() {
    CHECK(CheckSfoIdentity(PackageSfo(kTitle, kContent, "gd", "01.00", "01.00")).build == &SupportedBuilds()[0]);
    CHECK(CheckSfoIdentity(PackageSfo(kTitle, kContent, "gp", "01.02", "01.00")).build == &SupportedBuilds()[1]);
    CHECK(CheckSfoIdentity(PackageSfo(kTitle, kContent, "gp", "01.01", "01.00")).state != GameFileState::kValid);
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
    const GameBuild* base = &SupportedBuilds()[0];
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

// The real packages: each is refused in the other's action before anything is unpacked.
void TestRealPackages(const fs::path& scratch) {
    const char* folder = std::getenv("TSR_TEST_PACKAGES");
    if (!folder || !*folder) {
        std::printf("skipped: TSR_TEST_PACKAGES is not set\n");
        return;
    }
    const GameRoots roots{scratch / "prepared", scratch / "image"};
    std::error_code ec;
    int seen = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() != ".pkg" || name.find("CUSA19610") == std::string::npos) {
            continue;
        }
        const bool update = name.find("Update") != std::string::npos;
        const auto time_before = fs::last_write_time(entry.path());
        const auto size_before = fs::file_size(entry.path());
        InstallHooks hooks;
        hooks.expected = update ? PackageKind::kBase : PackageKind::kUpdate;
        const InstallResult result = InstallFromPackage(entry.path(), roots, hooks);
        CHECK(result.status == (update ? InstallStatus::kExpectedBasePackage : InstallStatus::kExpectedUpdatePackage));
        CHECK(fs::last_write_time(entry.path()) == time_before);
        CHECK(fs::file_size(entry.path()) == size_before);
        CHECK(!fs::exists(roots.prepared));
        std::printf("package %s: refused in the other action, untouched\n", update ? "update" : "base");
        ++seen;
    }
    CHECK(seen == 2);
}

// The whole workflow on the real packages, into TSR_TEST_INSTALL: base, update over it, then back to the base without
// reinstalling. About 4.5 GB is written there and removed again; the packages are only read.
void TestRealInstall() {
    const char* folder = std::getenv("TSR_TEST_PACKAGES");
    const char* target = std::getenv("TSR_TEST_INSTALL");
    if (!folder || !*folder || !target || !*target) {
        std::printf("skipped: TSR_TEST_PACKAGES and TSR_TEST_INSTALL are not both set\n");
        return;
    }
    fs::path base_package, update_package;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder)) {
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() == ".pkg" && name.find("CUSA19610") != std::string::npos) {
            (name.find("Update") != std::string::npos ? update_package : base_package) = entry.path();
        }
    }
    CHECK(!base_package.empty() && !update_package.empty());
    const fs::path root = target;
    const GameRoots roots{root / "prepared", root / "image"};
    const GameBuild& base = SupportedBuilds()[0];
    const GameBuild& update = SupportedBuilds()[1];

    InstallHooks wants_base;
    wants_base.expected = PackageKind::kBase;
    InstallHooks wants_update;
    wants_update.expected = PackageKind::kUpdate;
    const InstallResult too_early = InstallFromPackage(update_package, roots, wants_update);
    CHECK(too_early.status == InstallStatus::kUpdateNeedsBase);

    const InstallResult installed = InstallFromPackage(base_package, roots, wants_base);
    std::printf("base: %s (%s), %zu files\n", DescribeInstall(installed.status).title.c_str(), installed.detail.c_str(), installed.files);
    CHECK(installed.status == InstallStatus::kInstalled);
    CHECK(installed.build == &base);
    CHECK(LastPreparedBuild(roots) == &base);
    const GameCheck base_check = CheckGameFiles(BuildPaths(roots, base));
    CHECK(base_check.state == GameFileState::kValid && base_check.build == &base);
    const std::string base_eboot = Sha256Hex(BuildPaths(roots, base).app0 / "eboot.bin");
    CHECK(base_eboot == base.eboot_sha256);

    const InstallResult updated = InstallFromPackage(update_package, roots, wants_update);
    std::printf("update: %s (%s), %zu files\n", DescribeInstall(updated.status).title.c_str(), updated.detail.c_str(), updated.files);
    CHECK(updated.status == InstallStatus::kInstalled);
    CHECK(updated.build == &update);
    CHECK(LastPreparedBuild(roots) == &update);
    const GameCheck update_check = CheckGameFiles(BuildPaths(roots, update));
    CHECK(update_check.state == GameFileState::kValid && update_check.build == &update);

    // The base survived the update, byte for byte where it matters, and is still a complete game.
    CHECK(Sha256Hex(BuildPaths(roots, base).app0 / "eboot.bin") == base.eboot_sha256);
    CHECK(!fs::exists(BuildPaths(roots, base).app0 / "ROBO_PATCH_US.CPK"));
    CHECK(CheckGameFiles(BuildPaths(roots, base)).build == &base);
    CHECK(PreparedBuilds(roots).size() == 2);

    CHECK(ActivateInstalledBuild(roots, base));
    CHECK(LastPreparedBuild(roots) == &base);
    CHECK(ActivateInstalledBuild(roots, update));
    CHECK(LastPreparedBuild(roots) == &update);
    std::error_code ec;
    fs::remove_all(root, ec);
}

// The local install (game\prepared and build\image), only read: valid as is, rebuilt byte for byte in scratch, and refused
// with a wrong content profile signature.
void TestLocalInstall(const fs::path& scratch) {
    const GameBuild& base = SupportedBuilds()[0];
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
    TestRealPackages(scratch);
    TestRealInstall();
    TestLocalInstall(scratch);
    fs::remove_all(scratch, ec);
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
