// The Bug Report page's work without a window: the logs it offers and the report it builds.
//
// A report is built into an IncidentPackage, whether or not the player previews it. The preview and the saved file both
// read that package's fields, so what the player can read is what is saved. Nothing is sent: the player attaches the
// saved file to a GitHub issue themselves.
#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "log_scrubber.h"

namespace tsr {

inline constexpr size_t kMostGameLogs = 5;
inline constexpr size_t kFewestDescriptionCharacters = 10;
inline constexpr size_t kMostDescriptionCharacters = 500;
inline constexpr size_t kMostLogBytes = 64 * 1024;

// How far a session got: kGameStarted left the launcher for the game, kGameplay reached the game's own code.
enum class LogActivity { kLauncher, kGameStarted, kGameplay };

// kNone is a session that started the game without reaching its own code.
enum class LogRole { kNone, kCurrentLauncher, kCurrentGameplay, kLastGameplay, kOtherGameplay, kOtherLauncher };

struct GameLog {
    std::filesystem::path path;
    std::string name;
    std::time_t written = 0;
    uintmax_t bytes = 0;
    LogActivity activity = LogActivity::kLauncher;
    LogRole role = LogRole::kNone;
};

// The logs in `folder`: `running_log` (this launcher's own session) first, then newest first. Left out: other files and
// sessions that closed in the launcher without starting the game.
std::vector<GameLog> FindGameLogs(const std::filesystem::path& folder, const std::filesystem::path& running_log,
                                  size_t most = kMostGameLogs);

// The log a report is most likely about: the newest that started the game, not this launcher's own session.
size_t DefaultGameLog(const std::vector<GameLog>& logs);

// English, for Tr; nullptr for kNone.
const char* LogRoleText(LogRole role);
const char* PreviewButtonText(bool preview_open);

// "Nov 5, 2026 03:51"; local time.
std::string LogDateText(std::time_t written);
// "184 KB", "1.2 MB".
std::string LogSizeText(uintmax_t bytes);

// The bytes of `text` that hold its first `most` UTF-8 characters.
size_t BytesOfCharacters(std::string_view text, size_t most);
size_t CountCharacters(std::string_view text);

enum class DraftProblem { kNone, kDescriptionTooShort, kNoLog };
// Spaces around the description do not count toward its length.
DraftProblem CheckDraft(std::string_view description, bool has_log);

// The fields of the installed game and this PC that go with every report.
struct ReportFacts {
    std::string launcher_version;
    std::string game_build;
    std::string game_version;
    std::string cusa;
    std::string eboot_sha256;
    std::string operating_system;
};

// "Windows 11 (build 26200)", "Steam Deck: SteamOS 3.6".
std::string OperatingSystemText();

struct IncidentField {
    std::string_view key;
    std::string_view label; // English; the preview translates it
    std::string value;
};

struct IncidentPackage {
    std::vector<IncidentField> fields;
    const std::string& Value(std::string_view key) const;
};

// The end of `text`, at most `most` bytes, starting on a whole line.
std::string LastBytes(std::string_view text, size_t most);

// Scrubs the log's text and bounds it. nullopt when the log cannot be read or holds nothing.
std::optional<IncidentPackage> BuildIncidentPackage(std::string_view description, const GameLog& log, const ReportFacts& facts,
                                                    const PrivateFacts& private_facts);

std::string IncidentPreviewText(const IncidentPackage& package);

// Writes the report as text into `folder`, named after the time; the file's path, empty when it could not be written.
std::filesystem::path SaveIncidentReport(const IncidentPackage& package, const std::filesystem::path& folder);

} // namespace tsr
