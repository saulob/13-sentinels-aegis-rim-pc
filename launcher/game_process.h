// Windows, with Allow overlay programs on: Play starts the game as a new process of this program, so overlay programs
// attach to a clean game process instead of inheriting what entered the launcher's. Off keeps the game in this process.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tsr {

inline constexpr char kGameProcessArgument[] = "--tsr-game";

#ifdef _WIN32
inline constexpr bool kSeparateGameProcessAvailable = true;
#else
inline constexpr bool kSeparateGameProcessAvailable = false;
#endif

bool SeparateGameProcessWanted(bool overlays_allowed, bool available = kSeparateGameProcessAvailable);

std::filesystem::path CurrentExecutablePath();
// This process's arguments, without the program.
std::vector<std::string> CurrentProcessArguments();

// launcher_arguments: this process's arguments, without the program. The game process gets them without the ones that
// choose the launcher or the log, then --tsr-game and, when log_file is set, --log <log_file> (it continues that log).
std::vector<std::string> GameProcessArguments(const std::vector<std::string>& launcher_arguments,
                                              const std::string& log_file);

#ifdef _WIN32
std::wstring WindowsCommandLine(const std::filesystem::path& executable, const std::vector<std::string>& arguments);
#endif

// Creates the game process suspended, in this process's folder and environment; tsr_resume_game_process starts it.
// False, with the reason, when nothing was created.
bool CreateGameProcess(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                       std::string& error);

// Whether a process other than this one runs `executable`, such as a game this launcher started earlier.
bool OtherProcessRunning(const std::filesystem::path& executable);

} // namespace tsr
