// The loader's boot image (boot.bin, content.bin and the image.json report) built from a prepared game tree: the
// eboot mapped, the game's bundled modules linked after it, every other import left by name for the host runtime.
// Same output as scripts/prepare_image.py.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace tsr {

// Must match the signatures src/loader.c checks.
inline constexpr std::string_view kBootImageMagic = "TSRBOOT1";
inline constexpr std::string_view kContentProfileMagic = "TSRCONT1";

// Writes the three files into `out` (created when missing). Empty on success, else the reason.
std::string BuildBootImage(const std::filesystem::path& game, const std::filesystem::path& out);

} // namespace tsr
