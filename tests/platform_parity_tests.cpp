// Windows and Linux offer the same features. The shared feature contract of the nine builds is computed from the
// tables both platforms compile (--contract FILE writes it; tools\package-release.ps1 requires the Windows and Linux
// files to be identical). The platform matrix checks that every platform-specific part has its counterpart, that the
// only Windows-only options are the listed ones and are not offered on Linux, and that the release names agree.
// Static and headless.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "controller_prompts.h"
#include "exit_game.h"
#include "game_files.h"
#include "game_process.h"
#include "game_profile.h"
#include "lzcnt_compat.h"
#include "settings.h"
#include "skip_intro.h"
#include "tsr_renderer.h"
#include "tsr_resolution.h"
#include "tsr_title.h"
#include "tsrgpu.h"
#include "update_config.h"
#include "updater/sha256.h"
#include "updater/version.h"

#ifndef TSR_PORT_VERSION
#error TSR_PORT_VERSION must name the project version
#endif

namespace fs = std::filesystem;
using namespace tsr;

namespace {

int checks = 0, failures = 0;
#define CHECK(condition)                                                                                                   \
    do {                                                                                                                   \
        ++checks;                                                                                                          \
        if (!(condition)) {                                                                                                \
            ++failures;                                                                                                    \
            std::printf("PARITY FAIL line %d (%s): %s\n", __LINE__, current.c_str(), #condition);                          \
        }                                                                                                                  \
    } while (0)

std::string current;

std::string Source(const std::string& relative) {
    std::ifstream in(fs::path(TSR_SOURCE_DIR) / relative, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::erase(text, '\r');
    return text;
}

bool Has(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

std::string Hex(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "%llx", static_cast<unsigned long long>(value));
    return text;
}

std::string Sha256(const void* data, size_t size) {
    return trg::update::Sha256Hex(std::string_view(static_cast<const char*>(data), size));
}

// The image the loader runs, followed by room for hook stubs within a rel32 jump (as tests/build_parity_tests.cpp).
struct Image {
    std::vector<uint8_t> bytes;
    uint64_t size = 0, entry = 0;
};

uint64_t Le64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | p[i];
    return v;
}

bool LoadImage(const fs::path& folder, Image& image) {
    std::ifstream in(folder / "boot.bin", std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (file.size() < 8 * 16 || std::memcmp(file.data(), "TSRBOOT1", 8)) return false;
    image.size = Le64(&file[8]);
    image.entry = Le64(&file[16]);
    if (image.size > file.size()) return false;
    image.bytes.assign(file.end() - std::ptrdiff_t(image.size), file.end());
    image.bytes.resize(image.size + (1u << 20), 0xcc);
    return true;
}

// Where the features land on the real code: the same bytes on both platforms, since the tables and locators are shared.
void RealImageLines(const GameBuild& build, std::vector<std::string>& out) {
    const char* folder = std::getenv("TSR_TEST_IMAGES");
    if (!folder || !*folder) return;
    Image image;
    const bool loaded = LoadImage(fs::path(folder) / build.folder, image);
    CHECK(loaded); // TSR_TEST_IMAGES set: every build's image is required
    if (!loaded) {
        out.push_back(std::string("image ") + build.folder + " missing");
        return;
    }
    const std::string code = CodeBuildKey(build);
    uint32_t intro = 0;
    const SkipIntroLocate intro_status = skip_intro_locate(image.bytes.data(), image.size, code.c_str(), &intro);
    CHECK(intro_status == SKIP_INTRO_FOUND);
    ExitGameSites exit{};
    const ExitGameLocate exit_status = exit_game_locate(image.bytes.data(), image.size, code.c_str(), &exit, nullptr);
    CHECK(exit_status == EXIT_GAME_FOUND);
    std::string exit_line = "image.exit_game";
    for (uint32_t read : exit.flag_reads) exit_line += " " + Hex(read);
    exit_line += " confirm=" + Hex(exit.confirm_dispatch) + " layout=" + Hex(exit.layout_bind) + " texture=" + Hex(exit.texture_copy);
    char error[256] = "";
    std::vector<uint8_t> scratch = image.bytes;
    const int resolution = tsr_apply_resolution(scratch.data(), image.size, build.resolution_sites.data(), build.resolution_sites.size(),
                                                &tsr_resolutions[TSR_RESOLUTION_COUNT - 1], error, sizeof(error));
    CHECK(resolution == 1);
    const LzcntBuild* lzcnt = nullptr;
    for (const LzcntBuild& known : lzcnt_builds) {
        if (known.image.size == image.size && known.image.entry == image.entry) lzcnt = &known;
    }
    CHECK(lzcnt != nullptr);
    const int lzcnt_status = lzcnt ? lzcnt_compat_install(scratch.data(), image.size, image.entry, scratch.data() + image.size,
                                                          lzcnt_compat_stub_bytes(), nullptr)
                                   : -1;
    CHECK(lzcnt_status == LZCNT_COMPAT_OK);
    out.push_back("image.identity size=" + Hex(image.size) + " entry=" + Hex(image.entry));
    out.push_back("image.skip_intro " + Hex(intro));
    out.push_back(exit_line);
    out.push_back("image.patched_sha256 " + Sha256(scratch.data(), image.size) + " (4K render size and LZCNT rewrite applied)");
}

// The features every platform must have identically, as text: any difference between the Windows and the Linux file is
// a feature one of them lacks.
std::vector<std::string> FeatureContract() {
    std::vector<std::string> out;
    out.push_back(std::string("port ") + TSR_PORT_VERSION);
    out.push_back(std::string("title ") + kGameTitle + " window=" + TSR_WINDOW_TITLE);
    out.push_back(std::string("bug_report ") + kBugReportServiceUrl);
    out.push_back("supported " + SupportedBuildsText());
    std::string resolutions = "render_resolutions";
    for (const TsrResolution& r : tsr_resolutions) {
        resolutions += " " + std::string(r.key) + "=" + std::to_string(r.width) + "x" + std::to_string(r.height) +
                       (r.pro_memory ? "/pro" : "");
    }
    out.push_back(resolutions);
    out.push_back("title_bar " + TsrTitle::Compose({.base = TsrTitle::Base(TSR_WINDOW_TITLE, "CUSA19610 1.02"),
                                                    .show_resolution = true, .width = 2560, .height = 1440,
                                                    .show_fps = true, .fps = 60.0}));
    size_t asset_size = 0;
    const uint8_t* asset = exit_game_embedded_asset(&asset_size);
    out.push_back("exit_game.labels sha256=" + Sha256(asset, asset_size));
    out.push_back("lzcnt.stub_bytes " + std::to_string(lzcnt_compat_stub_bytes()));
    out.push_back("builds " + std::to_string(SupportedBuilds().size()));
    for (const GameBuild& build : SupportedBuilds()) {
        current = build.label;
        const PackageLayer& finished = FinishedLayer(build);
        const GameBuild* base = BaseBuildOf(build);
        out.push_back(std::string("build ") + build.folder + " label=" + build.label + " title_id=" + build.title_id +
                      " region=" + build.region + " content_id=" + build.content_id + " app_ver=" + finished.app_ver +
                      " sfo_version=" + finished.sfo_version + " base=" + (base ? BuildKey(*base) : "-") +
                      " code=" + CodeBuildKey(build));
        out.push_back(std::string("  eboot ") + build.eboot_sha256 + " archive=" + build.regional_archive);
        std::string languages = "  languages";
        for (const TextLanguage& language : build.text_languages) {
            languages += " " + std::string(language.key) + ":" + std::to_string(language.ps4_id) + ":" + std::string(language.archive);
        }
        out.push_back(languages);
        out.push_back(std::string("  voices japanese=") + build.voice_archive + " english=" +
                      (build.english_voices ? build.english_voice_archive : "-"));
        const std::span<const uint8_t> prompts = XboxPromptsPatch(build);
        CHECK(!prompts.empty());
        out.push_back(std::string("  xbox_prompts ") + build.prompt_patch + " sha256=" + Sha256(prompts.data(), prompts.size()));
        std::string sites = "  render_sites " + std::to_string(build.resolution_sites.size());
        for (const TsrResolutionSite& site : build.resolution_sites) sites += " " + Hex(site.offset) + ":" + Hex(site.value);
        out.push_back(sites);
        std::vector<uint8_t> blank(0x10000);
        uint32_t site = 0;
        // A known key on code that is not there is a mismatch; an unknown key would mean the build lost the feature.
        const bool skip_intro = skip_intro_locate(blank.data(), blank.size(), CodeBuildKey(build).c_str(), &site) == SKIP_INTRO_MISMATCH;
        const bool exit_game = exit_game_locate(blank.data(), blank.size(), CodeBuildKey(build).c_str(), nullptr, nullptr) ==
                               EXIT_GAME_SITE_MISMATCH;
        CHECK(skip_intro && exit_game);
        out.push_back(std::string("  skip_intro ") + (skip_intro ? "known" : "MISSING") + " exit_game " + (exit_game ? "known" : "MISSING"));
        const LzcntBuild* lzcnt = nullptr;
        for (const LzcntBuild& known : lzcnt_builds) {
            if (CodeBuildKey(build) == known.build) lzcnt = &known;
        }
        CHECK(lzcnt != nullptr);
        out.push_back(lzcnt ? "  lzcnt sites=" + std::to_string(lzcnt->count) + " image=" + Hex(lzcnt->image.size) + "/" +
                                  Hex(lzcnt->image.entry)
                            : "  lzcnt MISSING");
        out.push_back(std::string("  saves user/savedata/<user>/") + build.title_id);
        out.push_back("  window " + TsrTitle::Base(TSR_WINDOW_TITLE, std::string(build.title_id) + " " + build.version));
        std::vector<std::string> image;
        RealImageLines(build, image);
        for (const std::string& line : image) out.push_back("  " + line);
    }
    current = "";
    return out;
}

// A platform-specific part of a shared feature: the source that has it on Windows and the one on Linux.
struct Counterpart {
    const char* feature;
    const char* windows_file;
    const char* windows_text;
    const char* linux_file;
    const char* linux_text;
};

constexpr Counterpart kCounterparts[] = {
    {"guest memory", "src/runtime_memory.c", "VirtualAlloc2", "src/runtime_memory.c", "memfd_create"},
    {"faults and crash report", "src/loader.c", "AddVectoredExceptionHandler", "src/loader.c", "sigaction(SIGSEGV"},
    {"logs", "src/runtime_log.c", "freopen(path", "src/runtime_log_posix.c", "dup2("},
    {"hashes and ciphers", "launcher/crypto_win.cpp", "BCrypt", "launcher/crypto_openssl.cpp", "EVP_"},
    {"Bug Report HTTPS", "launcher/http_post.cpp", "WinHttpSendRequest", "launcher/http_post.cpp", "SSL_connect"},
    {"portable data folder", "launcher/paths.cpp", "GetModuleFileNameW", "launcher/paths.cpp", "getenv(\"APPIMAGE\")"},
    {"file chooser", "third_party/trg-launcher/src/platform.cpp", "GetOpenFileNameW", "launcher/launcher.cpp", "zenity"},
    {"open folder", "third_party/trg-launcher/src/platform.cpp", "ShellExecute", "third_party/trg-launcher/src/platform.cpp", "xdg-open"},
    {"Shift at start", "launcher/shift_key.cpp", "trg::ShiftHeld()", "launcher/shift_key.cpp", "XQueryKeymap"},
    {"update download", "third_party/trg-launcher/src/download.cpp", "WinHttpOpen", "third_party/trg-launcher/src/download.cpp", "\"curl\""},
    {"self-update install", "launcher/update_config.cpp", "trg-updater.exe", "launcher/update_config.cpp", "HasOriginalAppImage"},
    {"self-update replace", "launcher/updater/install_transaction.cpp", "MoveFileExW", "launcher/updater/update_platform.cpp",
     "ApplyAppImageUpdate(const std::filesystem::path& downloaded"},
    {"restart after update", "launcher/updater/update_platform.cpp", "CreateProcessW", "launcher/updater/update_platform.cpp", "posix_spawn("},
    {"show launcher", "src/loader.c", "!strcmp(argv[i], \"--launcher\")", "packaging/linux/13-sentinels-aegis-rim-pc.desktop", "Exec=AppRun --launcher"},
};

// Offered on Windows only, by technical necessity, and never offered on Linux.
void TestWindowsOnly() {
    current = "Windows-only options";
    CHECK(kSeparateGameProcessAvailable == (std::string_view(TSR_PLATFORM) == "windows"));
    CHECK(TsrRenderer::Direct3D11Available == (std::string_view(TSR_PLATFORM) == "windows"));
    bool d3d11_offered = false;
    for (const RendererChoice& choice : kRendererChoices) d3d11_offered = d3d11_offered || choice.key == "d3d11";
    CHECK(d3d11_offered == TsrRenderer::Direct3D11Available);
    CHECK(TsrRenderer::ParseChoice("d3d11", false) == TsrRenderer::Vulkan); // a d3d11 tsr.ini copied to Linux keeps Vulkan
    const std::string launcher = Source("launcher/launcher.cpp");
    CHECK(Has(launcher, "if (kSeparateGameProcessAvailable) {\n            ui.Toggle(\"Allow overlay programs\""));
    // The Direct3D 11 entry of the launcher's and the in-game menu's renderer lists sits inside #ifdef _WIN32.
    const auto windows_only_entry = [](const std::string& text) {
        const size_t entry = text.find("{\"d3d11\", \"Direct3D 11 (experimental)\"}");
        if (entry == std::string::npos) return false;
        const size_t opened = text.rfind("#ifdef _WIN32", entry), closed = text.rfind("#endif", entry);
        return opened != std::string::npos && (closed == std::string::npos || closed < opened) &&
               text.find("#endif", entry) != std::string::npos;
    };
    CHECK(windows_only_entry(Source("gpu/shim/tsr_options_menu.h")));
    CHECK(windows_only_entry(Source("launcher/settings.h")));
    // ANGLE is staged by the Windows package only; the Linux package refuses anything like it.
    CHECK(Has(Source("tools/package.ps1"), "Sync-AngleRuntime") && !Has(Source("tools/package-linux.sh"), "angle"));
    current = "";
}

void TestCounterparts() {
    for (const Counterpart& c : kCounterparts) {
        current = c.feature;
        const std::string windows = Source(c.windows_file);
        const std::string linux_source = Source(c.linux_file);
        CHECK(!windows.empty() && !linux_source.empty());
        CHECK(Has(windows, c.windows_text) && Has(linux_source, c.linux_text));
    }
    current = "";
}

// The Linux side of a shared feature is never a stub that refuses: only these platform messages exist.
void TestNoLinuxStubs() {
    current = "Linux stubs";
    const std::set<std::string> allowed = {
        "The update helper is only used on Windows.",                     // the AppImage replaces itself in-process
        "The game runs in the launcher's process on this system.",         // Allow overlay programs (Windows only)
        "Automatic updates are only available in the Linux AppImage build.", // Windows stub of the AppImage path
        "Automatic installation is available in the AppImage build.",      // a Linux build run outside its AppImage
    };
    const char* files[] = {"launcher/launcher.cpp", "launcher/game_process.cpp", "launcher/update_config.cpp", "launcher/http_post.cpp",
                           "launcher/game_install.cpp", "launcher/paths.cpp", "launcher/file_browser.cpp", "launcher/incident_report.cpp",
                           "launcher/log_scrubber.cpp", "launcher/trophy_page.cpp", "launcher/settings.cpp",
                           "launcher/updater/update_platform.cpp", "launcher/updater/update_controller.cpp", "src/loader.c",
                           "src/runtime_file.c", "src/runtime_savedata.c", "src/runtime_audio.c", "src/runtime_pad.c"};
    for (const char* file : files) {
        const std::string text = Source(file);
        CHECK(!text.empty());
        for (const char* phrase : {"only available", "only used on", "on this system", "available in the"}) {
            for (size_t at = text.find(phrase); at != std::string::npos; at = text.find(phrase, at + 1)) {
                const size_t open = text.rfind('"', at), close = text.find('"', at);
                const std::string message = text.substr(open + 1, close - open - 1);
                if (!allowed.count(message)) {
                    std::printf("  %s: \"%s\"\n", file, message.c_str());
                    CHECK(allowed.count(message));
                }
            }
        }
    }
    current = "";
}

// Every patch and hook source is the same code on both platforms: no platform conditionals, and every one of them is
// in the sources of the game on both.
void TestSharedPatches() {
    current = "shared patches";
    const char* shared[] = {"src/skip_intro.c", "src/exit_game.c", "src/exit_game_assets.c", "src/guest_hook.c", "src/code_signature.c",
                            "src/lzcnt_compat.c", "src/isa_emulate.c", "launcher/game_profile.cpp", "launcher/controller_prompts.cpp",
                            "launcher/cpk_patch.cpp", "launcher/resolution.c", "launcher/language_support.cpp",
                            "gpu/shadps4/video_core/amdgpu/command_snapshot.h", "src/runtime_tsr.c", "src/runtime_ajm.c"};
    for (const char* file : shared) {
        current = file;
        const std::string text = Source(file);
        CHECK(!text.empty());
        CHECK(!Has(text, "_WIN32") && !Has(text, "__linux__") && !Has(text, "__unix__"));
    }
    current = "CMake";
    const std::string cmake = Source("CMakeLists.txt");
    CHECK(Has(cmake, "set(TSR_HOOK_SOURCES src/guest_hook.c src/code_signature.c src/skip_intro.c src/exit_game.c src/exit_game_assets.c"));
    CHECK(Has(cmake, "set(TSR_GAME_SOURCES src/loader.c src/isa_emulate.c src/lzcnt_compat.c ${TSR_PLATFORM_SOURCES} ${TSR_RUNTIME_SOURCES}\n    ${TSR_HOOK_SOURCES})"));
    CHECK(Has(cmake, "set(TSR_PLATFORM_SOURCES src/runtime_log_posix.c)"));
    current = "";
}

std::vector<std::string> ListedTargets(const std::string& text, std::string_view open, std::string_view close) {
    std::vector<std::string> names;
    const size_t start = text.find(open);
    if (start == std::string::npos) return names;
    const std::string body = text.substr(start, text.find(close, start + open.size()) - start);
    for (size_t at = body.find("tsr-"); at != std::string::npos; at = body.find("tsr-", at + 1)) {
        size_t end = at;
        while (end < body.size() && (std::isalnum(uint8_t(body[end])) || body[end] == '-')) ++end;
        if (end - at > 10 && body.compare(end - 6, 6, "-tests") == 0 && (at == 0 || body[at - 1] != '-')) {
            names.push_back(body.substr(at, end - at));
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

// tools\test.ps1 and tools/build.sh --test run the same headless tests, apart from the Windows-only ones.
void TestSameTests() {
    current = "test lists";
    const std::vector<std::string> windows = ListedTargets(Source("tools/test.ps1"), "$steps = @(", "\n)\n");
    const std::vector<std::string> linux_tests = ListedTargets(Source("tools/build.sh"), "linux_tests=(", "\n)\n");
    const std::set<std::string> windows_only = {"tsr-memory-windows-tests", "tsr-log-tests", "tsr-update-helper-tests"};
    CHECK(windows.size() > 20 && linux_tests.size() > 20);
    for (const std::string& name : windows) {
        const bool listed = windows_only.count(name) || std::binary_search(linux_tests.begin(), linux_tests.end(), name);
        if (!listed) std::printf("  %s runs on Windows only\n", name.c_str());
        CHECK(listed);
    }
    for (const std::string& name : linux_tests) {
        const bool listed = std::binary_search(windows.begin(), windows.end(), name);
        if (!listed) std::printf("  %s runs on Linux only\n", name.c_str());
        CHECK(listed && !windows_only.count(name));
    }
    current = "";
}

// Both artifacts come from the one project VERSION and carry the names the installed updaters look for.
void TestReleaseNames() {
    current = "release names";
    const std::string cmake = Source("CMakeLists.txt");
    CHECK(Has(cmake, std::string("project(thirteen_sentinels_aegis_rim_pc VERSION ") + TSR_PORT_VERSION + " "));
    const std::string updater = Source("launcher/update_config.cpp");
    CHECK(Has(updater, "\"13SentinelsAegisRimPC-v\" + version.CoreString() + \"-windows-x64.zip\""));
    CHECK(Has(updater, "\"13SentinelsAegisRimPC-v\" + version.CoreString() + \"-windows-x64.zip.sha256\""));
    CHECK(Has(updater, "\"13SentinelsAegisRimPC-v\" + version.CoreString() + \"-x86_64.AppImage\""));
    CHECK(Has(updater, "\"13SentinelsAegisRimPC-v\" + version.CoreString() + \"-x86_64.AppImage.sha256\""));
    CHECK(Has(Source("tools/package.ps1"), "$name = \"13SentinelsAegisRimPC-v$version-windows-x64\""));
    const std::string linux_package = Source("tools/package-linux.sh");
    CHECK(Has(linux_package, "name=13SentinelsAegisRimPC-v$version-x86_64"));
    CHECK(Has(linux_package, "s/^project\\(thirteen_sentinels_aegis_rim_pc VERSION ([0-9]+\\.[0-9]+\\.[0-9]+).*/\\1/p"));
    const std::string release = Source("tools/package-release.ps1");
    CHECK(Has(release, "$windowsName = \"13SentinelsAegisRimPC-v$version-windows-x64\""));
    CHECK(Has(release, "$linuxName = \"13SentinelsAegisRimPC-v$version-x86_64\""));

    const trg::update::UpdateConfig config = MakeUpdateConfig(fs::temp_directory_path());
    const std::optional<trg::update::SemanticVersion> version = trg::update::ParseVersion(TSR_PORT_VERSION);
    CHECK(version.has_value() && config.current_version == TSR_PORT_VERSION);
    if (version) {
        const bool windows = std::string_view(TSR_PLATFORM) == "windows";
        const std::string asset = std::string("13SentinelsAegisRimPC-v") + TSR_PORT_VERSION + (windows ? "-windows-x64.zip" : "-x86_64.AppImage");
        CHECK(config.asset_name(*version) == asset && config.checksum_asset_name(*version) == asset + ".sha256");
        CHECK(config.package_kind == (windows ? trg::update::UpdatePackageKind::kZip : trg::update::UpdatePackageKind::kAppImage));
    }
    current = "";
}

} // namespace

int main(int argc, char** argv) {
    const char* contract_file = nullptr;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--contract") contract_file = argv[i + 1];
    }
    const std::vector<std::string> contract = FeatureContract();
    CHECK(SupportedBuilds().size() == 9);
    TestWindowsOnly();
    TestCounterparts();
    TestNoLinuxStubs();
    TestSharedPatches();
    TestSameTests();
    TestReleaseNames();
    std::string text;
    for (const std::string& line : contract) text += line + "\n";
    if (contract_file) {
        std::ofstream(contract_file, std::ios::binary) << text;
        std::printf("feature contract: %s (%zu lines, sha256 %s)\n", contract_file, contract.size(), trg::update::Sha256Hex(text).c_str());
    }
    std::printf("platform parity (%s): %d checks, %d failed; contract sha256 %s%s\n", TSR_PLATFORM, checks, failures,
                trg::update::Sha256Hex(text).c_str(), std::getenv("TSR_TEST_IMAGES") ? ", real boot images included" : "");
    return failures ? 1 : 0;
}
