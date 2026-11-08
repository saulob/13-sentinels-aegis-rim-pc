// Block patches for files inside the game's CRI CPK archives (ROBO_*.CPK), applied in place and reversibly: the
// patched copy of an entry is appended, uncompressed, to the end of the archive and the entry's row in the table of
// contents is pointed at it. Nothing of the original data is overwritten, and an undo record next to the archive
// (<name>.CPK.undo) holds what revert needs: the original table of contents and header bytes and the original length.
// An archive hard-linked into another version's folder gets a file of its own first. The Controller icons setting is
// built on this; the format is shared with the Dragon's Crown Pro port's tools.
//
// A DCPX3 patch (scripts/prompts/make_dcpx.py) names, per archive, the entries it changes with the CRC32 of each before
// and after and the 16-byte runs that differ. An archive is patched whole or not at all: every entry the patch lists for
// it must be the file the patch was made for.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tsr {

struct BlockRun {
    uint32_t offset = 0;
    std::array<uint8_t, 16> data{};
};

struct BlockPatchTarget {
    std::string archive; // file name of the archive that holds the entry
    std::string name;    // path inside the archive, as the table of contents spells it: GUI/Interface.ftx
    uint32_t size = 0; // of the uncompressed entry
    uint32_t crc_before = 0;
    uint32_t crc_after = 0;
    std::vector<BlockRun> runs;
};

struct BlockPatch {
    std::vector<BlockPatchTarget> targets;
};

// Nothing when the bytes are not a DCPX3 patch; `error` says why. An entry may have several targets (one per language
// copy of the file): the archive's file is matched against each one's CRC.
std::optional<BlockPatch> ParseBlockPatch(std::span<const uint8_t> bytes, std::string* error = nullptr);

// One entry of an archive's table of contents.
struct CpkEntry {
    std::string path;      // DirName/FileName
    uint32_t size = 0;     // bytes stored
    uint32_t extract_size = 0;
    uint64_t offset = 0;   // in the archive
    uint32_t id = 0;
    size_t row = 0;        // row in the TOC table
    bool compressed() const { return size < extract_size; }
};

// Reads the header and table of contents of a CPK archive; entries are read on demand.
class CpkArchive {
public:
    // False when the file is not a CPK the reader understands; `error` says why.
    bool Open(const std::filesystem::path& file, std::string* error = nullptr);
    // The same from any seekable stream; the archive then has no file, so entries cannot be read.
    bool OpenStream(std::istream& in, std::string* error = nullptr);
    const std::vector<CpkEntry>& entries() const { return entries_; }
    const CpkEntry* Find(std::string_view path) const; // case-insensitive, nothing when absent
    // The entry's data, decompressed when it is stored CRILAYLA-compressed; empty with `error` set on failure.
    std::vector<uint8_t> Read(const CpkEntry& entry, std::string* error = nullptr) const;

    const std::filesystem::path& file() const { return file_; }
    uint64_t toc_offset() const { return toc_offset_; }
    uint64_t toc_base() const { return toc_base_; }
    uint32_t align() const { return align_; }
    // The header's EnableTocCrc value (0 when the column is absent).
    uint32_t enable_toc_crc() const { return enable_toc_crc_; }

private:
    friend struct PatchWriter;
    std::filesystem::path file_;
    std::vector<CpkEntry> entries_;
    uint64_t toc_offset_ = 0, toc_base_ = 0, content_offset_ = 0;
    uint32_t align_ = 0x800;
    uint32_t enable_toc_crc_ = 0;
    // The raw packets (decrypted) and where their per-row numeric fields sit, for rewriting.
    struct Field {
        size_t pos;
        int type;
    };
    struct Table {
        bool encrypted = false;
        uint64_t file_pos = 0; // of the packet in the archive (after the 16-byte section header)
        std::vector<uint8_t> packet;
        std::vector<std::vector<std::pair<std::string, Field>>> fields; // per row
        const Field* field(size_t row, std::string_view name) const {
            if (row >= fields.size()) return nullptr;
            for (const auto& [n, f] : fields[row]) {
                if (n == name) return &f;
            }
            return nullptr;
        }
    };
    Table header_, toc_;
};

// Decompresses one CRILAYLA block as stored in a CPK entry; empty with `error` set when it is malformed.
std::vector<uint8_t> CrilaylaDecompress(std::span<const uint8_t> data, std::string* error = nullptr);

enum class ArchivePatchState { kOriginal, kPatched };

// Hard-linked archives are normally given a file of their own before they are written; keep_links writes through the
// link instead, for a file whose every link the caller manages (and updates the undo records of).
struct PatchOptions {
    bool keep_links = false;
};

// The patch's targets for one archive file.
BlockPatch PatchForArchive(const BlockPatch& patch, const std::filesystem::path& cpk);

enum class EntryState {
    kOriginal, // the file the patch was made for
    kPatched,  // already the patch's result
    kUnknown,  // neither: another version of the file
    kMissing,  // not in the archive
};

struct EntryReport {
    std::string name;
    EntryState state = EntryState::kMissing;
};

// Each entry the patch lists for this archive, read from it.
std::vector<EntryReport> InspectArchive(const std::filesystem::path& cpk, const BlockPatch& patch);

// Every entry the patch lists for the archive is its result and the undo record names exactly those entries.
bool ArchiveHolds(const std::filesystem::path& cpk, const BlockPatch& patch);

std::filesystem::path UndoPath(const std::filesystem::path& cpk);
// Patched when the undo record next to the archive exists.
ArchivePatchState ArchiveState(const std::filesystem::path& cpk);
// The entries the archive's undo record says were rewritten; empty when it is not patched or the record is unreadable.
std::vector<std::string> PatchedEntries(const std::filesystem::path& cpk);

struct PatchHooks {
    std::function<void(const char* text)> phase;                     // "Reading stage/Interface_US.ftx..."
    std::function<bool(uint64_t done, uint64_t total)> progress;     // false cancels
};

struct PatchResult {
    bool ok = false;
    bool cancelled = false;
    std::string detail;   // for the log
    size_t patched = 0;   // entries rewritten (0 with ok when the archive already held the patch)
    uint64_t appended = 0;
};

// Applies the patch's targets for this archive, all or nothing: an entry that is missing or not the file the patch was
// made for refuses the whole archive (not ok, nothing written). An archive that already holds the patch is left alone; one
// holding another patch (by its undo record) is first put back as it was.
PatchResult ApplyBlockPatch(const std::filesystem::path& cpk, const BlockPatch& patch, const PatchHooks& hooks = {},
                            const PatchOptions& options = {});
// Puts the archive back as it was, from the undo record, and removes the record.
PatchResult RevertBlockPatch(const std::filesystem::path& cpk, const PatchOptions& options = {});

struct OverlayResult {
    bool ok = false;
    bool wrote = false;            // false with ok when the update changes nothing: `output` was not created
    bool update_unreadable = false;
    std::string detail;            // for the log
    size_t replaced = 0, added = 0, kept = 0; // update entries laid over a base entry, added, and left out by the filter
    uint64_t appended = 0;
};

// Writes to `output` the archive `base` with the entries of the archive `update` laid over it: an entry of the same path
// replaces the base's, a new path is added, and an entry `keep_base` names is left out. The base file is only read.
// The entries' stored bytes are appended to a copy of the base and its table of contents rewritten; a table that outgrows
// its place moves to the end of the file.
OverlayResult OverlayArchive(const std::filesystem::path& base, const std::filesystem::path& update,
                             const std::function<bool(std::string_view path)>& keep_base,
                             const std::filesystem::path& output);

// What a reader of the archive sees when entries are replaced the way ApplyBlockPatch does it, without writing the file:
// the new data appended past `length` (the archive's size) and the rewritten table of contents and header, by position.
struct ArchiveOverlay {
    uint64_t length = 0;
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> patches;
};
// Nothing when the archive lacks one of the entries; `error` names it.
std::optional<ArchiveOverlay> OverlayEntries(const CpkArchive& archive, uint64_t length,
                                             std::span<const std::pair<std::string, std::vector<uint8_t>>> entries,
                                             std::string* error = nullptr);

uint32_t Crc32(std::span<const uint8_t> bytes);

} // namespace tsr
