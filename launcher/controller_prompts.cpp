#include "controller_prompts.h"

#include <algorithm>
#include <cstdio>
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

std::string Hex32(uint32_t value) {
    char text[9];
    std::snprintf(text, sizeof(text), "%08x", value);
    return text;
}

std::string Where(const GameBuild& build, const fs::path& archive) {
    return std::string("build ") + build.label + ", archive " + Utf8(archive.filename());
}

void AddLine(std::vector<std::string>* lines, std::string line) {
    if (lines) {
        lines->push_back(std::move(line));
    }
}

bool NoteMissingArchives(const fs::path& app0, const GameBuild& build, std::vector<std::string>* lines) {
    bool any = false;
    std::error_code ec;
    for (const char* name : build.prompt_archives) {
        if (!fs::is_regular_file(app0 / name, ec)) {
            AddLine(lines, std::string("Controller prompts missing archive: build ") + build.label + ", archive " + name);
            any = true;
        }
    }
    return any;
}

// Inspects every entry, so the log names each file that keeps the patch out, not only the first.
bool EntriesFit(const GameBuild& build, const fs::path& archive, const BlockPatch& patch, std::string& why,
                std::vector<std::string>* lines) {
    const std::string name = Utf8(archive.filename());
    size_t original = 0, patched = 0, total = 0;
    bool fits = true;
    const auto refuse = [&](std::string text) {
        if (fits) {
            why = std::move(text);
        }
        fits = false;
    };
    for (const EntryReport& report : InspectArchive(archive, patch)) {
        ++total;
        original += report.state == EntryState::kOriginal;
        patched += report.state == EntryState::kPatched;
        if (report.state == EntryState::kMissing) {
            refuse(report.name + " is not in " + name);
            AddLine(lines, "Controller prompts missing asset: " + Where(build, archive) + ", entry " + report.name + ": " +
                               report.analysis);
        } else if (report.state == EntryState::kUnknown) {
            refuse(report.name + " in " + name + " is not the file the patch was made for");
            AddLine(lines, std::string(report.damaged_patch ? "Controller prompts partial or damaged patch: "
                                                            : "Controller prompts unsupported asset: ") +
                               Where(build, archive) + ", entry " + report.name + ", size " + std::to_string(report.size) +
                               ", CRC32 " + Hex32(report.crc));
            AddLine(lines, "Controller prompts " + report.analysis + " (archive " + name + ", entry " + report.name + ")");
        }
    }
    if (fits && patched && original) {
        refuse(name + " holds part of the patch without an undo record");
        AddLine(lines, "Controller prompts partial patch: " + Where(build, archive) + ": " + std::to_string(patched) + " of " +
                           std::to_string(total) + " entries already carry the patch and no undo record names them");
    }
    return fits;
}

// An archive is back as it was when it has no undo record and none of the patch's entries still reads as patched.
std::string RestoreProblem(const fs::path& archive, const BlockPatch& patch) {
    const std::string name = Utf8(archive.filename());
    if (ArchiveState(archive) == ArchivePatchState::kPatched) {
        return name + ": the undo record is still there";
    }
    for (const EntryReport& report : InspectArchive(archive, patch)) {
        if (report.state == EntryState::kPatched) {
            return name + ": " + report.name + " is still patched";
        }
    }
    return {};
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
    static const struct {
        std::string_view key;
        const uint8_t* data;
        const size_t* size;
    } kPatches[] = {
        {"1.00", kXboxPrompts100Patch, &kXboxPrompts100PatchSize},
        {"1.02", kXboxPrompts102Patch, &kXboxPrompts102PatchSize},
        {"eu-1.00", kXboxPromptsEurope100Patch, &kXboxPromptsEurope100PatchSize},
        {"jp-1.00", kXboxPromptsJapan100Patch, &kXboxPromptsJapan100PatchSize},
        {"jp-1.03", kXboxPromptsJapan103Patch, &kXboxPromptsJapan103PatchSize},
        {"jp-1.04", kXboxPromptsJapan104Patch, &kXboxPromptsJapan104PatchSize},
        {"asia-1.00", kXboxPromptsAsia100Patch, &kXboxPromptsAsia100PatchSize},
    };
    for (const auto& patch : kPatches) {
        if (patch.key == build.prompt_patch) {
            return {patch.data, *patch.size};
        }
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

bool PromptPatchFits(const fs::path& app0, const GameBuild& build, const BlockPatch& patch, std::string* detail,
                     std::vector<std::string>* unsupported) {
    std::string first;
    const auto refuse = [&](std::string why) {
        if (first.empty()) {
            first = std::move(why);
        }
    };
    if (build.prompt_archives.empty()) {
        refuse("the build lists no prompt archive");
    }
    if (NoteMissingArchives(app0, build, unsupported)) {
        refuse("a prompt archive is missing");
    }
    for (const fs::path& archive : PromptArchives(app0, build)) {
        if (PatchForArchive(patch, archive).targets.empty()) {
            refuse("the patch lists nothing for " + Utf8(archive.filename()));
            AddLine(unsupported, "Controller prompts: the patch lists nothing for " + Where(build, archive));
            continue;
        }
        std::string why;
        if (!Revertible(archive) && !EntriesFit(build, archive, patch, why, unsupported)) {
            refuse(why);
        }
    }
    if (detail && !first.empty()) {
        *detail = first;
    }
    return first.empty();
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
        if (style == PromptStyle::kXbox) {
            NoteMissingArchives(app0, build, &result.unsupported);
        }
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
        if (std::string why; !PromptPatchFits(app0, build, *patch, &why, &result.unsupported)) {
            result.detail = "Xbox icons do not fit " + std::string(build.label) + ": " + why;
            return result;
        }
    }
    const auto rollback = [&](const std::vector<fs::path>& done) {
        std::string problems;
        for (const fs::path& archive : done) {
            const Links links = LinksOf(archive, linked_folders);
            const PatchResult undone = RevertBlockPatch(archive, links.options);
            SyncUndo(archive, links);
            const std::string problem = undone.ok ? RestoreProblem(archive, *patch) : undone.detail;
            if (problem.empty()) {
                ++result.rolled_back;
            } else {
                problems += (problems.empty() ? "" : "; ") + problem;
            }
        }
        result.rollback_incomplete = !problems.empty();
        note("rollback: " + std::to_string(result.rolled_back) + " of " + std::to_string(done.size()) +
             " archives read back as original" + (problems.empty() ? "" : "; not restored: " + problems));
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
