// Launcher logic without a window: tsr.ini persistence (also as the game reads it), value validation, the launcher
// gate, key bindings, text languages and window sizing. How the launcher looks stays a manual check.
#include <SDL3/SDL_scancode.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "game_process.h"
#include "game_profile.h"
#include "language_support.h"
#include "options.h"
#include "settings.h"
#include "tsr_keys.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

void TestPersistence(const fs::path& scratch) {
    const fs::path file = scratch / "tsr.ini";
    {
        std::ofstream out(file);
        out << "# written by hand\ncustom_key = keep me\nvsync = 0\n";
    }
    LauncherSettings settings(file);
    CHECK(settings.Get(kVSyncKey) == "0");
    settings.Set(kVSyncKey, "1");
    settings.Set(kDisplayModeKey, "borderless");
    settings.Set(kLauncherWidthKey, "1400");
    CHECK(Read(file).find("vsync = 0") != std::string::npos);
    CHECK(settings.Save());

    const std::string text = Read(file);
    CHECK(text.find("# written by hand") != std::string::npos);
    CHECK(text.find("custom_key = keep me") != std::string::npos);
    LauncherSettings reloaded(file);
    CHECK(reloaded.Get(kVSyncKey) == "1");
    CHECK(reloaded.Get(kDisplayModeKey) == "borderless");
    CHECK(reloaded.Get(kLauncherWidthKey) == "1400");

    reloaded.ResetToDefaults();
    CHECK(reloaded.Get(kDisplayModeKey) == "windowed");
    CHECK(reloaded.Get(kLauncherWidthKey) == "1400");
    CHECK(reloaded.SaveNow({{std::string(kShowFrameCounterKey), "1"}}));
    LauncherSettings partial(file);
    CHECK(partial.Get(kShowFrameCounterKey) == "1");
    CHECK(partial.Get(kDisplayModeKey) == "borderless");

    LauncherSettings missing(scratch / "nested" / "new.ini");
    missing.Set(kVSyncKey, "0");
    CHECK(missing.Save());
    CHECK(fs::exists(scratch / "nested" / "new.ini"));
}

void TestValidation(const fs::path& scratch) {
    CHECK(IsValidSetting(kDisplayModeKey, "windowed"));
    CHECK(!IsValidSetting(kDisplayModeKey, "fullscreen-ish"));
    CHECK(IsValidSetting(kAnisotropyKey, "16"));
    CHECK(!IsValidSetting(kAnisotropyKey, "3"));
    CHECK(IsValidSetting(kRenderResolutionKey, kNativeResolution));
    CHECK(!IsValidSetting(kRenderResolutionKey, "9999p"));
    CHECK(IsValidSetting(kPresentationKey, kDefaultPresentation));
    CHECK(!IsValidSetting(kTextLanguageKey, "klingon"));
    CHECK(!IsValidSetting(tsr_key_actions[TSR_KEY_CROSS].ini_key, "NotAKey"));

    const fs::path file = scratch / "invalid.ini";
    {
        std::ofstream out(file);
        out << "display_mode = sideways\nanisotropy = 3\n";
    }
    LauncherSettings settings(file);
    CHECK(settings.Get(kDisplayModeKey, "windowed") == "windowed");
    CHECK(settings.Get(kAnisotropyKey, "16") == "16");
    CHECK(IsLauncherPlacementSetting(kLauncherXKey));
    CHECK(!IsLauncherPlacementSetting(kVSyncKey));
    for (const ManagedSetting& setting : ManagedSettings()) {
        if (!setting.default_value.empty()) {
            CHECK(IsValidSetting(setting.key, setting.default_value));
        }
    }
}

void TestLauncherGate() {
    CHECK(LauncherWanted(false, false, false, true));
    CHECK(LauncherWanted(true, true, false, false));
    CHECK(LauncherWanted(true, false, true, false));
    CHECK(!LauncherWanted(true, false, false, false));
    CHECK(!LauncherWanted(true, true, true, true));
}

// The Controls page writes a key; the game reads the same file with its own reader.
void TestKeyBindings(const fs::path& scratch) {
    const fs::path file = scratch / "keys.ini";
    LauncherSettings settings(file);
    CHECK(SavedKey(settings, TSR_KEY_CROSS) == tsr_key_actions[TSR_KEY_CROSS].default_scancode);
    SetKey(settings, TSR_KEY_CROSS, SDL_SCANCODE_Z);
    SetKey(settings, TSR_KEY_OPTIONS, SDL_SCANCODE_RETURN);
    CHECK(SavedKey(settings, TSR_KEY_CROSS) == SDL_SCANCODE_Z);
    CHECK(settings.Save());

    TsrKeyMap map;
    tsr_keys_defaults(&map);
    tsr_keys_load(file.string().c_str(), &map);
    CHECK(map.scancode[TSR_KEY_CROSS] == SDL_SCANCODE_Z);
    CHECK(map.scancode[TSR_KEY_OPTIONS] == SDL_SCANCODE_RETURN);
    CHECK(map.scancode[TSR_KEY_CIRCLE] == tsr_key_actions[TSR_KEY_CIRCLE].default_scancode);

    ResetKeys(settings);
    for (int action = 0; action < TSR_KEY_COUNT; ++action) {
        CHECK(SavedKey(settings, static_cast<TsrKeyAction>(action)) == tsr_key_actions[action].default_scancode);
    }
    settings.Set(tsr_key_actions[TSR_KEY_SQUARE].ini_key, "NotAKey");
    CHECK(SavedKey(settings, TSR_KEY_SQUARE) == tsr_key_actions[TSR_KEY_SQUARE].default_scancode);
    char name[32];
    CHECK(tsr_key_from_name(tsr_key_name(SDL_SCANCODE_SPACE, name, sizeof(name))) == SDL_SCANCODE_SPACE);
    CHECK(KeyDisplayName(SDL_SCANCODE_SPACE) == "Space");
}

void TestTextLanguages(const fs::path& scratch) {
    for (const GameBuild& build : SupportedBuilds()) {
        const auto languages = TextLanguagesOf(&build);
        CHECK(!languages.empty());
        CHECK(ResolveTextLanguage(languages, "no such language").key == languages.front().key);
        for (const TextLanguage& language : languages) {
            CHECK(ResolveTextLanguage(languages, language.key).key == language.key);
            CHECK(IsValidSetting(kTextLanguageKey, language.key));
        }
    }
    const auto languages = TextLanguagesOf(&SupportedBuilds()[0]);
    CHECK(languages.size() >= 2);
    LauncherSettings settings(scratch / "language.ini");
    settings.Set(kTextLanguageKey, "klingon");
    CHECK(settings.Get(kTextLanguageKey, languages.front().key) == languages.front().key);
    // A language this install lacks falls back for the game, and the stored choice stays.
    settings.Set(kTextLanguageKey, std::string(languages.front().key));
    LanguageSupport support;
    support.text.assign(languages.begin() + 1, languages.end());
    CHECK(EffectiveTextLanguage(support, settings.Get(kTextLanguageKey)).key == support.text.front().key);
    CHECK(settings.Get(kTextLanguageKey) == languages.front().key);
}

void TestRenderer(const fs::path& scratch) {
    CHECK(std::string(kRendererKey) == "renderer" && std::string(kDefaultRenderer) == "vulkan");
    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
        managed |= setting.key == kRendererKey && setting.default_value == "vulkan";
    }
    CHECK(managed);
    for (const char* value : {"auto", "vulkan", "opengl"}) {
        CHECK(IsValidSetting(kRendererKey, value));
    }
    for (const char* invalid : {"", "OpenGL", "direct3d", "D3D11"}) {
        CHECK(!IsValidSetting(kRendererKey, invalid));
    }
#ifdef _WIN32
    CHECK(IsValidSetting(kRendererKey, "d3d11"));
    CHECK(RendererName("d3d11") == "Direct3D 11 (experimental)");
#else
    CHECK(!IsValidSetting(kRendererKey, "d3d11"));
#endif
    CHECK(RendererName("opengl") == "OpenGL (experimental)");
    CHECK(RendererName("vulkan") == "Vulkan");
    CHECK(RendererName("bogus") == "Vulkan");

    const fs::path file = scratch / "renderer.ini";
    CHECK(LauncherSettings(file).Get(kRendererKey, kDefaultRenderer) == "vulkan");
    {
        LauncherSettings settings(file);
        settings.Set(kRendererKey, "opengl");
        CHECK(settings.Save());
    }
    CHECK(LauncherSettings(file).Get(kRendererKey, kDefaultRenderer) == "opengl");
    {
        std::ofstream out(file);
        out << "renderer = metal\n";
    }
    CHECK(LauncherSettings(file).Get(kRendererKey, kDefaultRenderer) == "vulkan");
}

// The Play page no longer offers manual version switching or update removal: installing the highest version and
// letting it run automatically is the only workflow (launcher.cpp, PagePlay).
void TestNoVersionSwitchingUi() {
    const std::string launcher = Read(fs::path(TSR_SOURCE_DIR) / "launcher" / "launcher.cpp");
    CHECK(!launcher.empty());
    CHECK(launcher.find("GameVersionCombo") == std::string::npos);
    CHECK(launcher.find("\"Game version\"") == std::string::npos);
    CHECK(launcher.find("RemoveUpdateRows") == std::string::npos);
    CHECK(launcher.find("\"Remove update ") == std::string::npos);
    CHECK(launcher.find("void SelectBuild(") == std::string::npos);
    // The Play page still offers exactly Install Game, Install Update and Play.
    const size_t play_page = launcher.find("void PagePlay(");
    CHECK(play_page != std::string::npos);
    CHECK(launcher.find("Install game...", play_page) != std::string::npos);
    CHECK(launcher.find("Install update...", play_page) != std::string::npos);
    CHECK(launcher.find("kUpdateHelp", play_page) != std::string::npos);
}

// The freeze reported when switching game versions: RefreshPrompts() opened and parsed every prompt archive's CPK
// table of contents synchronously on the GUI thread. It must run on a worker thread (trg::Task), like installing
// and patching already did, and be polled every frame.
void TestPromptsRefreshIsAsync() {
    const std::string launcher = Read(fs::path(TSR_SOURCE_DIR) / "launcher" / "launcher.cpp");
    CHECK(!launcher.empty());
    CHECK(launcher.find("trg::Task refresh_;") != std::string::npos);
    CHECK(launcher.find("void PollPromptsRefresh()") != std::string::npos);
    CHECK(launcher.find("PollPromptsRefresh();", launcher.find("Outcome Frame()")) != std::string::npos);
    const size_t refresh = launcher.find("void RefreshPrompts()");
    const size_t start = launcher.find("refresh_.Start(", refresh);
    CHECK(refresh != std::string::npos && start != std::string::npos);
    // The archive inspection itself (InstalledPromptState / PromptPatchFits) runs inside that worker's job, not
    // directly in RefreshPrompts() on the caller's (GUI) thread.
    CHECK(launcher.find("InstalledPromptState(", refresh) > start);
    CHECK(launcher.find("PromptPatchFits(", refresh) > start);
}

void TestWindowSizes() {
    Display display;
    display.bounds = {0, 0, 1920, 1080};
    display.usable = {0, 0, 1920, 1040};
    display.scale = 1.5f;
    const WindowSize requested{2560, 1440};
    const WindowSize fitted = FitGameWindow(requested, display);
    CHECK(fitted.width <= 1920 && fitted.height <= 1040);
    CHECK(fitted.width > 0 && fitted.height > 0);
    CHECK(std::abs(fitted.width * 9 - fitted.height * 16) <= 16);
    CHECK(!WindowSizeFits(requested, display));
    CHECK(WindowSizeFits(fitted, display));
    CHECK(FitGameWindow({1280, 720}, display) == (WindowSize{1280, 720}));
    const std::vector<WindowSize> sizes = WindowSizes();
    CHECK(sizes.size() == std::size(kWindowHeights));
    for (const WindowSize size : sizes) {
        CHECK(std::abs(size.width * 9 - size.height * 16) <= 9);
    }
    CHECK(sizes[2] == kGamePictureSize);
}

// The update install waits while another process runs this installation's program, such as the game.
void TestOtherProcessRunning() {
    const fs::path self = tsr::CurrentExecutablePath();
    CHECK(!tsr::OtherProcessRunning(self));
    CHECK(!tsr::OtherProcessRunning(self.parent_path() / "not-running.exe"));
#ifdef _WIN32
    std::wstring command = L"\"" + self.wstring() + L"\" --hold";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    CHECK(CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                         &startup, &process));
    if (process.hProcess) {
        CHECK(tsr::OtherProcessRunning(self));
        CHECK(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
        CHECK(!tsr::OtherProcessRunning(self));
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--hold") {
#ifdef _WIN32
        Sleep(1500);
#endif
        return 0;
    }
    const fs::path scratch = fs::temp_directory_path() / "tsr-launcher-tests";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch);
    TestPersistence(scratch);
    TestValidation(scratch);
    TestLauncherGate();
    TestKeyBindings(scratch);
    TestTextLanguages(scratch);
    TestRenderer(scratch);
    TestNoVersionSwitchingUi();
    TestPromptsRefreshIsAsync();
    TestWindowSizes();
    TestOtherProcessRunning();
    fs::remove_all(scratch, ec);
    std::printf("launcher: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
