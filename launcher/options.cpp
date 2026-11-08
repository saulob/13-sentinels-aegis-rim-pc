#include "options.h"

#include <SDL3/SDL_keyboard.h>

#include "localization/ui_text.h"

namespace tsr {
namespace {

constexpr char kTimes[] = " \xC3\x97 ";

std::string Dimensions(int width, int height) {
    return std::to_string(width) + kTimes + std::to_string(height);
}

} // namespace

std::vector<WindowSize> WindowSizes() {
    std::vector<WindowSize> sizes;
    for (const int height : kWindowHeights) {
        sizes.push_back({(height * 16 + 4) / 9, height});
    }
    return sizes;
}

const char* PageName(PageId page) {
    switch (page) {
    case PageId::kPlay: return "Play";
    case PageId::kDisplay: return "Display";
    case PageId::kGraphics: return "Graphics";
    case PageId::kGameplay: return "Gameplay";
    case PageId::kControls: return "Controls";
    case PageId::kTrophies: return "Trophies";
    case PageId::kBugReport: return "Bug Report";
    case PageId::kAbout: return "About";
    }
    return "";
}

WindowSize FitGameWindow(WindowSize requested, const Display& display) {
    FitToDisplay(requested.width, requested.height, display);
    return requested;
}

bool WindowSizeFits(WindowSize size, const Display& display) {
    return FitGameWindow(size, display) == size;
}

std::string ResolutionLabel(const TsrResolution& resolution) {
    return std::string(Tr(resolution.name)) + " \xE2\x80\x94 " + Dimensions(resolution.width, resolution.height);
}

std::string WindowSizeLabel(WindowSize size) {
    std::string label = Dimensions(size.width, size.height);
    if (size == kGamePictureSize) {
        label += "   ";
        label += Tr("(the game's own size)");
    }
    return label;
}

std::string MonitorLabel(int monitor, const std::vector<Display>& displays) {
    const std::string number = std::to_string(monitor);
    if (monitor < 1 || monitor > static_cast<int>(displays.size())) {
        return Tr("Display ") + number + Tr(" (not connected)");
    }
    const Display& display = displays[static_cast<size_t>(monitor - 1)];
    std::string label = Tr("Display ") + number + (display.primary ? Tr(" (primary)") : "") + "   " +
                        Dimensions(display.bounds.w, display.bounds.h);
    if (!display.name.empty() && display.name != "Generic PnP Monitor") {
        label += "   " + display.name;
    }
    return label;
}

std::string KeyDisplayName(int scancode) {
    if (scancode == SDL_SCANCODE_RETURN) {
        return "Enter";
    }
    return tsr_key_bindable(scancode) ? SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode)) : std::string();
}

namespace {

std::string DefaultKeyName(TsrKeyAction action) {
    char name[32];
    return tsr_key_name(tsr_key_actions[action].default_scancode, name, sizeof(name));
}

} // namespace

int SavedKey(const trg::SettingsStore& settings, TsrKeyAction action) {
    const int saved = tsr_key_from_name(settings.Get(tsr_key_actions[action].ini_key).c_str());
    return saved ? saved : tsr_key_actions[action].default_scancode;
}

void SetKey(trg::SettingsStore& settings, TsrKeyAction action, int scancode) {
    char name[32];
    if (tsr_key_name(scancode, name, sizeof(name))[0]) {
        settings.Set(tsr_key_actions[action].ini_key, name);
    }
}

void ResetKeys(trg::SettingsStore& settings) {
    for (int action = 0; action < TSR_KEY_COUNT; ++action) {
        settings.Set(tsr_key_actions[action].ini_key, DefaultKeyName(static_cast<TsrKeyAction>(action)));
    }
}

} // namespace tsr
