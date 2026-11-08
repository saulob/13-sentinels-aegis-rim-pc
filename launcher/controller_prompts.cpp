#include "controller_prompts.h"

#include <algorithm>
#include <system_error>

#include "prompts_patch.h"
#include "utf8.h"

namespace tsr {
namespace {

namespace fs = std::filesystem;

struct Links {
    PatchOptions options;
    std::vector<fs::path> others; // the same file in the other builds' folders
};

// The archive's other hard links, when every one of them is in a folder the caller manages.
Links LinksOf(const fs::path& archive, const std::vector<fs::path>& folders) {
    Links links;
    std::error_code ec;
    const uintmax_t count = fs::hard_link_count(archive, ec);
    if (ec || count <= 1) {
        return links;
    }
    for (const fs::path& folder : folders) {
        const fs::path other = folder / archive.filename();
        if (other != archive && fs::is_regular_file(other, ec) && fs::equivalent(other, archive, ec)) {
            links.others.push_back(other);
        }
    }
    links.options.keep_links = count == 1 + links.others.size();
    return links;
}

// The undo record of a shared archive is kept next to every link: each folder then sees the archive as patched.
void SyncUndo(const fs::path& archive, const Links& links) {
    if (!links.options.keep_links) {
        return;
    }
    std::error_code ec;
    const bool patched = fs::is_regular_file(UndoPath(archive), ec);
    for (const fs::path& other : links.others) {
        const fs::path undo = UndoPath(other);
        if (!patched) {
            fs::remove(undo, ec);
        } else if (!fs::exists(undo, ec) || !fs::equivalent(undo, UndoPath(archive), ec)) {
            fs::copy_file(UndoPath(archive), undo, fs::copy_options::overwrite_existing, ec);
        }
    }
}

bool Revertible(const fs::path& archive) {
    return ArchiveState(archive) == ArchivePatchState::kPatched;
}

bool ReadsBackPatched(const fs::path& archive, const BlockPatch& patch) {
    const std::vector<EntryReport> reports = InspectArchive(archive, patch);
    return !reports.empty() && std::all_of(reports.begin(), reports.end(),
                                           [](const EntryReport& r) { return r.state == EntryState::kPatched; });
}

bool EntriesFit(const fs::path& archive, const BlockPatch& patch, std::string& why) {
    for (const EntryReport& report : InspectArchive(archive, patch)) {
        if (report.state == EntryState::kMissing) {
            why = report.name + " is not in " + Utf8(archive.filename());
            return false;
        }
        if (report.state == EntryState::kUnknown) {
            why = report.name + " in " + Utf8(archive.filename()) + " is not the file the patch was made for";
            return false;
        }
    }
    return true;
}

} // namespace

const char* PromptStyleKey(PromptStyle style) {
    return style == PromptStyle::kXbox ? kPromptsXbox : kPromptsPlayStation;
}

const char* PromptStyleLabel(PromptStyle style) {
    return style == PromptStyle::kXbox ? "Xbox" : "PlayStation (original)";
}

std::optional<PromptStyle> ParsePromptStyle(std::string_view key) {
    if (key == kPromptsPlayStation) {
        return PromptStyle::kPlayStation;
    }
    if (key == kPromptsXbox) {
        return PromptStyle::kXbox;
    }
    return std::nullopt;
}

std::span<const uint8_t> XboxPromptsPatch(const GameBuild& build) {
    const std::string_view version = build.version;
    if (version == "1.00" || version == "1.01") {
        return {kXboxPrompts100Patch, kXboxPrompts100PatchSize};
    }
    if (version == "1.02") {
        return {kXboxPrompts102Patch, kXboxPrompts102PatchSize};
    }
    return {};
}

std::vector<fs::path> PromptArchives(const fs::path& app0, const GameBuild& build) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const char* name : build.prompt_archives) {
        const fs::path archive = app0 / name;
        if (fs::is_regular_file(archive, ec)) {
            out.push_back(archive);
        }
    }
    return out;
}

bool PromptPatchFits(const fs::path& app0, const GameBuild& build, const BlockPatch& patch, std::string* detail) {
    const auto refuse = [&](std::string why) {
        if (detail) {
            *detail = std::move(why);
        }
        return false;
    };
    const std::vector<fs::path> archives = PromptArchives(app0, build);
    if (build.prompt_archives.empty() || archives.size() != build.prompt_archives.size()) {
        return refuse("a prompt archive is missing");
    }
    for (const fs::path& archive : archives) {
        if (PatchForArchive(patch, archive).targets.empty()) {
            return refuse("the patch lists nothing for " + Utf8(archive.filename()));
        }
        std::string why;
        if (!Revertible(archive) && !EntriesFit(archive, patch, why)) {
            return refuse(why);
        }
    }
    return true;
}

bool XboxPromptsAvailable(const fs::path& app0, const GameBuild& build, std::span<const uint8_t> xbox_patch) {
    const std::optional<BlockPatch> patch = ParseBlockPatch(xbox_patch);
    return patch && PromptPatchFits(app0, build, *patch);
}

PromptState InstalledPromptState(const fs::path& app0, const GameBuild& build, std::span<const uint8_t> xbox_patch) {
    const std::vector<fs::path> archives = PromptArchives(app0, build);
    const std::optional<BlockPatch> patch = ParseBlockPatch(xbox_patch);
    if (!patch || archives.empty() || archives.size() != build.prompt_archives.size()) {
        return PromptState::kPlayStation;
    }
    size_t holding = 0, touched = 0;
    for (const fs::path& archive : archives) {
        const bool holds = ArchiveHolds(archive, *patch);
        holding += holds;
        touched += holds || Revertible(archive);
    }
    if (holding == archives.size()) {
        return PromptState::kXbox;
    }
    return touched ? PromptState::kOther : PromptState::kPlayStation;
}

PromptStyle InstalledPromptStyle(const fs::path& app0, const GameBuild& build, std::span<const uint8_t> xbox_patch) {
    return InstalledPromptState(app0, build, xbox_patch) == PromptState::kXbox ? PromptStyle::kXbox
                                                                              : PromptStyle::kPlayStation;
}

PromptsResult SetPromptStyle(const fs::path& app0, const GameBuild& build, PromptStyle style,
                             std::span<const uint8_t> xbox_patch, const PatchHooks& hooks,
                             const std::vector<fs::path>& linked_folders) {
    PromptsResult result;
    const auto note = [&](const std::string& text) { result.detail += (result.detail.empty() ? "" : "; ") + text; };
    const std::vector<fs::path> archives = PromptArchives(app0, build);
    if (archives.empty() || archives.size() != build.prompt_archives.size()) {
        result.detail = "a prompt archive of " + std::string(build.label) + " is missing in " + Utf8(app0);
        return result;
    }
    std::optional<BlockPatch> patch;
    if (style == PromptStyle::kXbox) {
        std::string error;
        patch = ParseBlockPatch(xbox_patch, &error);
        if (!patch) {
            result.detail = "no Xbox icons for " + std::string(build.label) + (error.empty() ? "" : ": " + error);
            return result;
        }
        if (std::string why; !PromptPatchFits(app0, build, *patch, &why)) {
            result.detail = "Xbox icons do not fit " + std::string(build.label) + ": " + why;
            return result;
        }
    }
    const auto rollback = [&](const std::vector<fs::path>& done) {
        for (const fs::path& archive : done) {
            const Links links = LinksOf(archive, linked_folders);
            const PatchResult undone = RevertBlockPatch(archive, links.options);
            SyncUndo(archive, links);
            note("rolled back " + undone.detail);
        }
    };
    std::vector<fs::path> touched;
    for (const fs::path& archive : archives) {
        const std::string file = Utf8(archive.filename());
        const Links links = LinksOf(archive, linked_folders);
        if (hooks.phase) {
            hooks.phase(((patch ? "Patching " : "Restoring ") + file + "...").c_str());
        }
        PatchResult step;
        if (patch) {
            step = ApplyBlockPatch(archive, *patch, hooks, links.options);
            if (step.ok && !ReadsBackPatched(archive, *patch)) {
                step.ok = false;
                step.detail = file + ": an entry does not read back as the patch's result";
            }
        } else if (Revertible(archive)) {
            step = RevertBlockPatch(archive, links.options);
        } else {
            step.ok = true;
            step.detail = file + ": original";
        }
        SyncUndo(archive, links);
        note(step.detail);
        touched.push_back(archive);
        if (!step.ok) {
            result.cancelled = step.cancelled;
            if (patch) {
                rollback(touched);
            }
            return result;
        }
    }
    result.ok = true;
    return result;
}

} // namespace tsr
