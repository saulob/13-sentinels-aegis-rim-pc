// Installs the game from the player's own fake PKG (or an already extracted game folder): checks it, unpacks or lays
// it out in staging folders next to the prepared game, builds the boot image, checks the result and only then swaps it
// in. Settings, saves, logs and the shader cache live elsewhere and are never touched.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "game_files.h"

namespace tsr {

enum class InstallStatus {
    kInstalled,
    kCancelled,
    kNotPackage,
    kUnsupportedPackage,
    kNotGameFolder,
    kWrongGame,
    kWrongVersion,
    kIncomplete,
    kReadFailed,
    kOverlapping,
    kForeignFolder,
    kNoSpace,
    kWriteFailed,
    kPrepareFailed,
    kUpdateNeedsBase,
    kUpToDate,
    kUnsupportedBuild,
    kExpectedBasePackage,
    kExpectedUpdatePackage,
};

// What the player asked to install. An update and a base package are refused for each other's action.
enum class PackageKind { kAny, kBase, kUpdate };

struct InstallResult {
    InstallStatus status = InstallStatus::kInstalled;
    std::string detail; // for the log, not the UI
    std::vector<std::string> diagnostics; // kUnsupportedBuild: the block for the log
    uint64_t bytes = 0;
    size_t files = 0, linked = 0;
    const GameBuild* build = nullptr; // the build that was installed
    bool ok() const {
        return status == InstallStatus::kInstalled || status == InstallStatus::kUpToDate;
    }
};

struct InstallHooks {
    std::function<void(const char* phase)> phase;
    // Returns false to cancel. Cancelling is possible until the new files are activated.
    std::function<bool(uint64_t done, uint64_t total)> progress;
    bool replace_active = false; // install the package even when its build is the active, ready one
    PackageKind expected = PackageKind::kAny;
};

// The build is the one the source identifies as; it is installed into BuildPaths(roots, build). An update is laid over the
// installed base build of its title (kUpdateNeedsBase when that is not installed). On any failure or cancellation that
// build's current installation is left as it was. The source is a .pkg file or a game folder.
InstallResult InstallGame(const std::filesystem::path& source, const GameRoots& roots, const InstallHooks& hooks = {},
                          std::span<const GameBuild> builds = SupportedBuilds());
InstallResult InstallFromPackage(const std::filesystem::path& package, const GameRoots& roots,
                                 const InstallHooks& hooks = {}, std::span<const GameBuild> builds = SupportedBuilds());
InstallResult InstallFromFolder(const std::filesystem::path& source, const GameRoots& roots,
                                const InstallHooks& hooks = {}, std::span<const GameBuild> builds = SupportedBuilds());

// Lays an unpacked update over an installed base game in `staged_app0`, which must not exist yet. The base game is not
// changed: its files are linked, and the update's files replace the links.
InstallResult StageUpdate(const std::filesystem::path& base_app0, const std::filesystem::path& update_dir,
                          const std::filesystem::path& staged_app0);

// True when this build's own folder is completely and correctly prepared. There is nothing to "activate": the
// effective active build is always the highest-precedence one LastPreparedBuild finds ready, automatically.
bool ActivateInstalledBuild(const GameRoots& roots, const GameBuild& build, std::span<const GameBuild> builds = SupportedBuilds());

enum class RemoveStatus { kRemoved, kNotUpdate, kNotInstalled, kForeignFolder, kInUse };

struct RemoveResult {
    RemoveStatus status = RemoveStatus::kRemoved;
    std::string detail;            // for the log, not the UI
    bool base_selected = false;    // the base is the effective active build after this removal
    bool ok() const { return status == RemoveStatus::kRemoved; }
};

// Removes an installed update build: its game folder and boot image, nothing else. The base build, the other updates,
// saves, trophies and settings live elsewhere or in other folders and are not touched; the update's files that are links
// into the base only lose that link. Both folders are first renamed aside, so a removal that cannot start changes nothing
// and one that stops halfway leaves no folder that passes as installed. The effective active build (LastPreparedBuild)
// is whichever remains highest in precedence; it is never forced.
RemoveResult RemoveInstalledUpdate(const GameRoots& roots, const GameBuild& build,
                                   std::span<const GameBuild> builds = SupportedBuilds());

// Deletes what an interrupted removal left aside. True when nothing is left.
bool FinishInterruptedRemovals(const GameRoots& roots, std::span<const GameBuild> builds = SupportedBuilds());

// True when the game was chosen in the wrong action: the update package in Install game, or the reverse.
bool RefusedPackageKind(InstallStatus status);

struct InstallMessage {
    std::string title;
    std::string text;
};
InstallMessage DescribeInstall(InstallStatus status);

} // namespace tsr
