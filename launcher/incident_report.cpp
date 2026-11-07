#include "incident_report.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

#include "localization/ui_text.h"
#include "utf8.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace tsr {
namespace fs = std::filesystem;
namespace {

constexpr std::string_view kLogPrefix = "13_sentinels_aegis_rim_";
constexpr std::string_view kLogSuffix = ".log";
constexpr size_t kFewestLogDigits = 3, kMostLogDigits = 10;
constexpr size_t kLogsLookedAt = 200;
constexpr size_t kSessionHeadBytes = 128 * 1024;
// More than the report keeps, so that scrubbing cannot leave it short.
constexpr size_t kLogTailBytes = 256 * 1024;

// src/loader.c, launcher/launcher.cpp and src/runtime_thread.c write these.
constexpr std::string_view kGameStartMarks[] = {"Launcher: closed, starting the game", "TSR: game build"};
constexpr std::string_view kLauncherClosedMark = "Launcher: closed";
constexpr std::string_view kGuestCodeMark = "Runtime: guest thread '";

constexpr char kDescriptionKey[] = "description";
constexpr char kLogTextKey[] = "log_text";

struct FieldName {
    std::string_view key;
    std::string_view label;
};
constexpr FieldName kFieldNames[] = {
    {kDescriptionKey, "Description"},
    {"launcher_version", "Launcher version"},
    {"game_build", "Game build"},
    {"game_version", "Game version"},
    {"cusa", "CUSA"},
    {"eboot_sha256", "Executable SHA-256"},
    {"operating_system", "Operating system"},
    {"log_file", "Selected log"},
    {"log_clipped", "Log shortened to its end"},
    {kLogTextKey, "Log after privacy filtering"},
};

constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

bool IsGameLogName(std::string_view name) {
    if (!name.starts_with(kLogPrefix) || !name.ends_with(kLogSuffix)) return false;
    name.remove_prefix(kLogPrefix.size());
    name.remove_suffix(kLogSuffix.size());
    return name.size() >= kFewestLogDigits && name.size() <= kMostLogDigits &&
           std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string ReadPart(const fs::path& file, uintmax_t offset, size_t size) {
    std::ifstream in(file, std::ios::binary);
    std::string text(size, '\0');
    in.seekg(std::streamoff(offset));
    in.read(text.data(), std::streamsize(size));
    text.resize(size_t(std::max<std::streamsize>(in.gcount(), 0)));
    return text;
}

struct Session {
    LogActivity activity = LogActivity::kLauncher;
    bool closed_in_launcher = false;
};

Session ReadSession(const fs::path& file) {
    const std::string head = ReadPart(file, 0, kSessionHeadBytes);
    const auto holds = [&](std::string_view mark) { return head.find(mark) != std::string::npos; };
    if (holds(kGuestCodeMark)) return {LogActivity::kGameplay, false};
    if (std::any_of(std::begin(kGameStartMarks), std::end(kGameStartMarks), holds)) return {LogActivity::kGameStarted, false};
    return {LogActivity::kLauncher, holds(kLauncherClosedMark)};
}

std::time_t SystemTimeOf(fs::file_time_type written) {
    const auto system = std::chrono::system_clock::now() +
                        std::chrono::duration_cast<std::chrono::system_clock::duration>(written - fs::file_time_type::clock::now());
    return std::chrono::system_clock::to_time_t(system);
}

struct Candidate {
    fs::path path;
    std::string name;
    fs::file_time_type written;
    uintmax_t bytes = 0;
    bool running = false;
};

bool IsRunningLog(const fs::path& file, const fs::path& running_log) {
    std::error_code ec;
    return !running_log.empty() && (file == running_log || fs::equivalent(file, running_log, ec));
}

// A directory listing lags behind a file that is still open, so the running log is asked for its own size and time.
std::vector<Candidate> LogCandidates(const fs::path& folder, const fs::path& running_log) {
    std::vector<Candidate> found;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry;
        const std::string name = Utf8(it->path().filename());
        if (!IsGameLogName(name) || !it->is_regular_file(entry) || it->is_symlink(entry)) continue;
        const bool running = IsRunningLog(it->path(), running_log);
        const uintmax_t bytes = running ? fs::file_size(it->path(), entry) : it->file_size(entry);
        const fs::file_time_type written = running ? fs::last_write_time(it->path(), entry) : it->last_write_time(entry);
        if (entry || (bytes == 0 && !running)) continue;
        found.push_back({it->path(), name, written, bytes, running});
    }
    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
        if (a.running != b.running) return a.running;
        return a.written != b.written ? a.written > b.written : a.name > b.name;
    });
    return found;
}

void MarkLastGameplay(std::vector<GameLog>& logs) {
    const auto played = [](const GameLog& log) { return log.activity == LogActivity::kGameplay; };
    const auto last = std::find_if(logs.begin(), logs.end(), played);
    if (last != logs.end() && last->role == LogRole::kNone) last->role = LogRole::kLastGameplay;
    for (GameLog& log : logs) {
        if (log.role != LogRole::kNone) continue;
        if (log.activity == LogActivity::kGameplay) log.role = LogRole::kOtherGameplay;
        else if (log.activity == LogActivity::kLauncher) log.role = LogRole::kOtherLauncher;
    }
}

std::string TrimmedDescription(std::string_view text) {
    constexpr std::string_view kSpace = " \t\r\n";
    const size_t first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(kSpace) - first + 1));
}

#ifdef _WIN32
std::string WindowsVersionText() {
    // GetVersionEx answers with the version the manifest declares, not the running one.
    using GetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto get_version = ntdll ? reinterpret_cast<GetVersion>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"))) : nullptr;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!get_version || get_version(&version) != 0) return "Windows";
    constexpr DWORD kFirstWindows11Build = 22000;
    const char* name = version.dwMajorVersion == 10 ? (version.dwBuildNumber >= kFirstWindows11Build ? "Windows 11" : "Windows 10")
                                                    : "Windows";
    return std::string(name) + " (build " + std::to_string(version.dwBuildNumber) + ")";
}
#else
std::string LinuxDistributionText() {
    std::ifstream in("/etc/os-release");
    constexpr std::string_view kKey = "PRETTY_NAME=";
    for (std::string line; std::getline(in, line);) {
        if (!line.starts_with(kKey)) continue;
        std::string value = line.substr(kKey.size());
        std::erase(value, '"');
        return value;
    }
    return {};
}
#endif

} // namespace

std::vector<GameLog> FindGameLogs(const fs::path& folder, const fs::path& running_log, size_t most) {
    std::vector<GameLog> logs;
    std::vector<Candidate> candidates = LogCandidates(folder, running_log);
    if (candidates.size() > kLogsLookedAt) candidates.resize(kLogsLookedAt);
    for (const Candidate& candidate : candidates) {
        if (logs.size() == most) break;
        const Session session = ReadSession(candidate.path);
        // A session that ended without closing the launcher stays: that is what a launcher crash leaves.
        if (session.closed_in_launcher && !candidate.running) continue;
        LogRole role = LogRole::kNone;
        if (candidate.running) {
            role = session.activity == LogActivity::kGameplay ? LogRole::kCurrentGameplay : LogRole::kCurrentLauncher;
        }
        logs.push_back({candidate.path, candidate.name, SystemTimeOf(candidate.written), candidate.bytes, session.activity, role});
    }
    MarkLastGameplay(logs);
    return logs;
}

size_t DefaultGameLog(const std::vector<GameLog>& logs) {
    const auto started = std::find_if(logs.begin(), logs.end(), [](const GameLog& log) { return log.activity != LogActivity::kLauncher; });
    if (started != logs.end()) return size_t(started - logs.begin());
    const auto earlier = std::find_if(logs.begin(), logs.end(), [](const GameLog& log) {
        return log.role != LogRole::kCurrentLauncher && log.role != LogRole::kCurrentGameplay;
    });
    return earlier == logs.end() ? 0 : size_t(earlier - logs.begin());
}

const char* LogRoleText(LogRole role) {
    switch (role) {
    case LogRole::kCurrentLauncher: return "Current launcher session";
    case LogRole::kCurrentGameplay: return "Current session \xC2\xB7 Launcher + gameplay";
    case LogRole::kLastGameplay: return "Last gameplay";
    case LogRole::kOtherGameplay: return "Gameplay session";
    case LogRole::kOtherLauncher: return "Launcher only";
    case LogRole::kNone: break;
    }
    return nullptr;
}

const char* PreviewButtonText(bool preview_open) { return preview_open ? "Close preview" : "Open preview"; }

std::string LogDateText(std::time_t written) {
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &written);
#else
    localtime_r(&written, &local);
#endif
    char text[40];
    std::snprintf(text, sizeof(text), "%s %d, %04d %02d:%02d", kMonths[std::clamp(local.tm_mon, 0, 11)], local.tm_mday,
                  local.tm_year + 1900, local.tm_hour, local.tm_min);
    return text;
}

std::string LogSizeText(uintmax_t bytes) {
    constexpr uintmax_t kKilobyte = 1024, kMegabyte = kKilobyte * 1024;
    char text[32];
    if (bytes < kMegabyte) {
        std::snprintf(text, sizeof(text), "%llu KB", static_cast<unsigned long long>(std::max<uintmax_t>(1, (bytes + kKilobyte / 2) / kKilobyte)));
    } else {
        std::snprintf(text, sizeof(text), "%.1f MB", double(bytes) / double(kMegabyte));
    }
    return text;
}

size_t BytesOfCharacters(std::string_view text, size_t most) {
    size_t characters = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (characters == most) return i;
        ++characters;
    }
    return text.size();
}

size_t CountCharacters(std::string_view text) {
    return size_t(std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

DraftProblem CheckDraft(std::string_view description, bool has_log) {
    if (CountCharacters(TrimmedDescription(description)) < kFewestDescriptionCharacters) return DraftProblem::kDescriptionTooShort;
    return has_log ? DraftProblem::kNone : DraftProblem::kNoLog;
}

std::string OperatingSystemText() {
#ifdef _WIN32
    return WindowsVersionText();
#else
    const std::string distribution = LinuxDistributionText();
    return distribution.empty() ? "Linux" : "Linux: " + distribution;
#endif
}

const std::string& IncidentPackage::Value(std::string_view key) const {
    static const std::string missing;
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const IncidentField& field) { return field.key == key; });
    return found == fields.end() ? missing : found->value;
}

std::string LastBytes(std::string_view text, size_t most) {
    if (text.size() <= most) return std::string(text);
    std::string_view tail = text.substr(text.size() - most);
    const size_t line = tail.find('\n');
    if (line != std::string_view::npos && line + 1 < tail.size()) tail.remove_prefix(line + 1);
    return std::string(tail);
}

std::optional<IncidentPackage> BuildIncidentPackage(std::string_view description, const GameLog& log, const ReportFacts& facts,
                                                    const PrivateFacts& private_facts) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(log.path, ec);
    if (ec || size == 0) return std::nullopt;
    const uintmax_t offset = size > kLogTailBytes ? size - kLogTailBytes : 0;
    std::string raw = ReadPart(log.path, offset, size_t(size - offset));
    if (raw.empty()) return std::nullopt;
    if (offset > 0) {
        const size_t line = raw.find('\n');
        if (line != std::string::npos) raw.erase(0, line + 1);
    }
    const std::string scrubbed = ScrubText(raw, private_facts);
    std::string text = LastBytes(scrubbed, kMostLogBytes);
    if (text.find_first_not_of(" \t\n") == std::string::npos) return std::nullopt;
    const bool clipped = offset > 0 || text.size() < scrubbed.size();

    std::string summary = ScrubText(TrimmedDescription(description), private_facts);
    summary.resize(BytesOfCharacters(summary, kMostDescriptionCharacters));
    if (summary.empty()) return std::nullopt;

    std::string values[] = {std::move(summary), facts.launcher_version, facts.game_build, facts.game_version, facts.cusa,
                            facts.eboot_sha256, facts.operating_system, log.name, clipped ? "yes" : "no", std::move(text)};
    IncidentPackage package;
    for (size_t i = 0; i < std::size(kFieldNames); ++i) package.fields.push_back({kFieldNames[i].key, kFieldNames[i].label, std::move(values[i])});
    return package;
}

std::string IncidentPreviewText(const IncidentPackage& package) {
    std::string out;
    for (const IncidentField& field : package.fields) {
        const std::string label = Tr(std::string(field.label));
        if (field.key == kDescriptionKey || field.key == kLogTextKey) {
            out += (out.empty() ? "" : "\n") + label + ":\n" + field.value + "\n";
            if (field.key == kDescriptionKey) out += "\n";
        } else {
            out += label + ": " + (field.value.empty() ? "-" : field.value) + "\n";
        }
    }
    return out;
}

fs::path SaveIncidentReport(const IncidentPackage& package, const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    const std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));
    fs::path file = folder / ("tsr_report_" + std::string(stamp) + ".txt");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << IncidentPreviewText(package);
    return out.flush() ? file : fs::path();
}

} // namespace tsr
