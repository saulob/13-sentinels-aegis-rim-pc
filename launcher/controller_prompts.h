// The Controller icons setting: which controller's buttons the game's prompts show. The PlayStation glyphs are the game's
// own; the Xbox ones are painted over them by a block patch applied to the build's archives (cpk_patch.h): the button
// atlas (GUI/Interface.ftx; 1.02 reads its own from the ROBO_PATCH2 archives) and the pages of the built-in manual
// (Manual/ManualNN.ftx), which carry their own icons. The choice lives in the game files and is reversible.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cpk_patch.h"
#include "game_profile.h"

namespace tsr {

enum class PromptStyle { kPlayStation, kXbox };

inline constexpr char kPromptsPlayStation[] = "playstation";
inline constexpr char kPromptsXbox[] = "xbox";

const char* PromptStyleKey(PromptStyle style);
const char* PromptStyleLabel(PromptStyle style);
std::optional<PromptStyle> ParsePromptStyle(std::string_view key);

// The build's Xbox patch (launcher/prompts/xbox-prompts-<version>.dcpx); 1.01 reads the same archives as 1.00.
std::span<const uint8_t> XboxPromptsPatch(const GameBuild& build);

// The build's prompt archives that exist in app0, in profile order.
std::vector<std::filesystem::path> PromptArchives(const std::filesystem::path& app0, const GameBuild& build);

// True when every prompt archive of the build is present and each entry the patch lists for it is the original file or the
// patch's result (or the archive carries a patch of its own that its undo record can take back); `detail` names the first
// that is not. Every archive is inspected: `unsupported` receives the log lines for each missing archive, missing or
// unsupported entry (build, archive, entry, size, CRC32 and why no target takes it) and each partly applied patch.
bool PromptPatchFits(const std::filesystem::path& app0, const GameBuild& build, const BlockPatch& patch,
                     std::string* detail = nullptr, std::vector<std::string>* unsupported = nullptr);

bool XboxPromptsAvailable(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> xbox_patch);

// What the installed archives hold: the original files, this patch in every archive, or something else (an earlier patch,
// or a patch in only some of them).
enum class PromptState { kPlayStation, kXbox, kOther };

PromptState InstalledPromptState(const std::filesystem::path& app0, const GameBuild& build,
                                 std::span<const uint8_t> xbox_patch);

// Xbox only when every archive holds exactly this patch.
PromptStyle InstalledPromptStyle(const std::filesystem::path& app0, const GameBuild& build,
                                 std::span<const uint8_t> xbox_patch);

struct PromptsResult {
    bool ok = false;
    bool cancelled = false;
    std::string detail; // for the log
    std::vector<std::string> unsupported; // log lines for the assets that keep the patch from fitting
    size_t rolled_back = 0;               // archives read back as original after a failed patch
    bool rollback_incomplete = false;     // an archive could not be shown to be original again
};

// Patches or reverts every prompt archive so the game shows `style`. Xbox is all or nothing: nothing is written unless
// every archive fits, a failure puts back the archives this call already patched and checks that each reads back as
// original (`rolled_back`, `rollback_incomplete`), and success means each archive was read back as the patch's result. `linked_folders` are the other installed builds' folders: an archive whose every hard link
// is in them (and in app0) is patched in place, with its undo record copied beside each link, instead of being copied.
PromptsResult SetPromptStyle(const std::filesystem::path& app0, const GameBuild& build, PromptStyle style,
                             std::span<const uint8_t> xbox_patch, const PatchHooks& hooks = {},
                             const std::vector<std::filesystem::path>& linked_folders = {});

} // namespace tsr
