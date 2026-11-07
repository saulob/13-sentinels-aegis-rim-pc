#include "settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <system_error>
#include <utility>
#include <vector>

#include "tsr_keys.h"
#include "tsr_launcher.h"
#include "tsr_resolution.h"
#include "game_files.h"
#include "game_process.h"
#include "utf8.h"

namespace tsr {
namespace {

constexpr ManagedSetting kBaseSettings[] = {
    {kShowLauncherKey, "1"},
    {kDisplayModeKey, "windowed"},
    {kWindowWidthKey, "1920"},
    {kWindowHeightKey, "1080"},
    {kMonitorKey, "1"},
    {kVSyncKey, "1"},
    {kPresentationKey, kDefaultPresentation},
    {kUpscaleKey, "0"},
    {kSharpenKey, "1"},
    {kSharpenStrengthKey, "75"},
    {kRenderResolutionKey, kNativeResolution},
    {kAspectRatioKey, kDefaultAspectRatio},
    {kGraphicsCardKey, ""},
    {kAntiAliasingKey, "0"},
    {kAnisotropyKey, "0"},
    {kTextLanguageKey, "english"},
    {kShowFpsTitleKey, "0"},
    {kShowResolutionTitleKey, "0"},
    {kShowFrameCounterKey, "0"},
    {kDetailedLoggingKey, "1"},
    {kAllowOverlayProgramsKey, "0"},
    {kLauncherXKey, ""},
    {kLauncherYKey, ""},
    {kLauncherWidthKey, ""},
    {kLauncherHeightKey, ""},
    {kLauncherMaximizedKey, "0"},
    {kLauncherFullscreenKey, "0"},
};

constexpr std::string_view kPlacementKeys[] = {kLauncherXKey,         kLauncherYKey,         kLauncherWidthKey,
                                                kLauncherHeightKey,    kLauncherMaximizedKey, kLauncherFullscreenKey};

constexpr char kFileHeader[] =
    "# 13 Sentinels: Aegis Rim user settings (written by the launcher; F11 saves the display mode).\n"
    "# Remove a line to use the default.\n";

} // namespace

std::span<const ManagedSetting> ManagedSettings() {
    static const std::vector<ManagedSetting> settings = [] {
        static std::deque<std::string> default_names;
        std::vector<ManagedSetting> all(std::begin(kBaseSettings), std::end(kBaseSettings));
        for (const TsrKeyInfo& action : tsr_key_actions) {
            char name[32];
            default_names.emplace_back(tsr_key_name(action.default_scancode, name, sizeof(name)));
            all.push_back({action.ini_key, default_names.back()});
        }
        return all;
    }();
    return settings;
}

bool IsValidSetting(std::string_view key, std::string_view value) {
    const auto one_of = [&](std::initializer_list<std::string_view> values) {
        return std::find(values.begin(), values.end(), value) != values.end();
    };
    if (std::any_of(std::begin(tsr_key_actions), std::end(tsr_key_actions),
                    [&](const TsrKeyInfo& action) { return key == action.ini_key; })) {
        return tsr_key_from_name(std::string(value).c_str()) != 0;
    }
    if (key == kDisplayModeKey) {
        return one_of({"windowed", "borderless"});
    }
    if (key == kAnisotropyKey) {
        return one_of({"0", "2", "4", "8", "16"});
    }
    if (key == kTextLanguageKey) {
        const std::span<const GameBuild> builds = SupportedBuilds();
        return std::any_of(builds.begin(), builds.end(), [&](const GameBuild& build) {
            return std::any_of(build.text_languages.begin(), build.text_languages.end(),
                               [&](const TextLanguage& language) { return value == language.key; });
        });
    }
    if (key == kPresentationKey) {
        return std::any_of(std::begin(kPresentationChoices), std::end(kPresentationChoices),
                           [&](const PresentationChoice& choice) { return value == choice.key; });
    }
    if (key == kRenderResolutionKey) {
        return std::any_of(std::begin(tsr_resolutions), std::end(tsr_resolutions),
                           [&](const TsrResolution& resolution) { return value == resolution.key; });
    }
    if (key == kAspectRatioKey) {
        return std::any_of(std::begin(tsr_aspect_ratios), std::end(tsr_aspect_ratios),
                           [&](const TsrAspectRatio& aspect) { return value == aspect.key; });
    }
    return true;
}

bool IsLauncherPlacementSetting(std::string_view key) {
    for (const std::string_view placement_key : kPlacementKeys) {
        if (placement_key == key) {
            return true;
        }
    }
    return false;
}

LauncherSettings::LauncherSettings(std::filesystem::path file) : file_(std::move(file)) {
    true_value = "1";
    false_value = "0";
    const trg::TextFileSettings stored(Utf8(file_));
    for (const ManagedSetting& setting : ManagedSettings()) {
        if (stored.Has(setting.key)) {
            saved_[std::string(setting.key)] = stored.Get(setting.key);
        }
    }
    pending_ = saved_;
}

std::string LauncherSettings::Get(std::string_view key, std::string_view def) const {
    const auto it = pending_.find(key);
    if (it == pending_.end() || it->second.empty() || !IsValidSetting(key, it->second)) {
        return std::string(def);
    }
    return it->second;
}

void LauncherSettings::Set(std::string_view key, std::string value) {
    pending_[std::string(key)] = std::move(value);
}

bool LauncherSettings::Save() {
    Values changes;
    for (const auto& [key, value] : pending_) {
        const auto saved = saved_.find(key);
        if (saved == saved_.end() || saved->second != value) {
            changes.emplace(key, value);
        }
    }
    if (changes.empty()) {
        return true;
    }
    if (!WriteFile(changes)) {
        return false;
    }
    for (const auto& [key, value] : changes) {
        saved_[key] = value;
    }
    return true;
}

bool LauncherSettings::SaveNow(const Values& values) {
    if (!WriteFile(values)) {
        return false;
    }
    for (const auto& [key, value] : values) {
        saved_[key] = value;
        pending_[key] = value;
    }
    return true;
}

void LauncherSettings::ResetToDefaults() {
    for (const ManagedSetting& setting : ManagedSettings()) {
        if (!IsLauncherPlacementSetting(setting.key)) {
            pending_[std::string(setting.key)] = std::string(setting.default_value);
        }
    }
}

bool LauncherSettings::WriteFile(const Values& changes) {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    if (!std::filesystem::exists(file_, ec)) {
        std::ofstream(file_) << kFileHeader;
    }
    trg::TextFileSettings file(Utf8(file_));
    for (const auto& [key, value] : changes) {
        file.Set(key, value);
    }
    return file.Save();
}

bool NormalizeTextLanguage(trg::SettingsStore& settings, std::span<const TextLanguage> languages) {
    const std::string saved = settings.Get(kTextLanguageKey, languages.front().key);
    const std::string_view effective = ResolveTextLanguage(languages, saved).key;
    if (saved == effective) {
        return false;
    }
    settings.Set(kTextLanguageKey, std::string(effective));
    return true;
}

bool LauncherWanted(bool game_ready, bool show_setting, bool shift_held, bool skip_once) {
    if (!game_ready) {
        return true;
    }
    return !skip_once && (shift_held || show_setting);
}

} // namespace tsr

extern "C" int tsr_render_resolution(const char* settings_file, const char* image_dir) {
    using namespace tsr;
    if (RestrictedToNativeResolution(BuildOfImageDir(image_dir))) {
        return 0;
    }
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    return tsr_resolution_index(settings.Get(kRenderResolutionKey, kNativeResolution).c_str());
}

extern "C" int tsr_aspect_ratio(const char* settings_file, const char* image_dir) {
    using namespace tsr;
    if (RestrictedToNativeResolution(BuildOfImageDir(image_dir))) {
        return 0;
    }
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    return tsr_aspect_index(settings.Get(kAspectRatioKey, kDefaultAspectRatio).c_str());
}

extern "C" int tsr_detailed_logging(const char* settings_file) {
    using namespace tsr;
    if (!settings_file || !*settings_file) {
        return 1;
    }
    const LauncherSettings settings(PathFromUtf8(settings_file));
    return settings.GetBool(kDetailedLoggingKey, true) ? 1 : 0;
}

namespace tsr {
namespace {

const char* OnOff(bool on) { return on ? "on" : "off"; }

} // namespace

std::string SettingsSummary(const LauncherSettings& settings, const GameBuild* build,
                            std::span<const TextLanguage> languages, const TsrRunSettings* run) {
    std::string out;
    const auto line = [&out](const std::string& text) { out += "Settings: " + text + "\n"; };
    const char* build_label = run ? run->game_build : build ? build->label : nullptr;
#ifdef _WIN32
    const char* platform = "Windows x64";
#else
    const char* platform = "Linux x86-64";
#endif
    line(std::string("13 Sentinels: Aegis Rim PC Port v" TSR_PORT_VERSION ", ") + platform + ", game build " +
         (build_label ? build_label : "not a supported build"));

    const std::string mode = settings.Get(kDisplayModeKey, "windowed");
    line(std::string("Window ") + (mode == "borderless" ? "Borderless Fullscreen" : "Windowed") + ", frame counter " +
         OnOff(settings.GetBool(kShowFrameCounterKey, false)) + ", frame rate in title " +
         OnOff(settings.GetBool(kShowFpsTitleKey, false)) + ", render resolution in title " +
         OnOff(settings.GetBool(kShowResolutionTitleKey, false)));
    if (run) {
        line("Render resolution " + std::to_string(run->render_width) + "x" + std::to_string(run->render_height) + " (" +
             run->render_resolution + "), aspect ratio " + run->aspect_ratio);
    } else {
        line("Render resolution " + settings.Get(kRenderResolutionKey, kNativeResolution) + ", aspect ratio " +
             settings.Get(kAspectRatioKey, kDefaultAspectRatio));
    }
    line(std::string("Menu & Subtitles ") + std::string(ResolveTextLanguage(languages, settings.Get(kTextLanguageKey)).name) +
         ", overlay programs " +
         (kSeparateGameProcessAvailable ? OnOff(settings.GetBool(kAllowOverlayProgramsKey, false)) : "unsupported"));
    return out;
}

} // namespace tsr

extern "C" void tsr_log_settings_summary(const char* settings_file, const char* image_dir, const char* app0,
                                         const TsrRunSettings* run) {
    using namespace tsr;
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    const GameBuild* build = BuildOfImageDir(image_dir);
    const std::vector<TextLanguage> languages = InstalledTextLanguages(PathFromUtf8(app0 ? app0 : ""), build);
    std::fputs(SettingsSummary(settings, build, languages, run).c_str(), stdout);
}

extern "C" int tsr_text_language(const char* settings_file, const char* image_dir, const char* app0) {
    using namespace tsr;
    const GameBuild* build = BuildOfImageDir(image_dir);
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    const std::vector<TextLanguage> languages = InstalledTextLanguages(PathFromUtf8(app0 ? app0 : ""), build);
    return ResolveTextLanguage(languages, settings.Get(kTextLanguageKey)).ps4_id;
}
