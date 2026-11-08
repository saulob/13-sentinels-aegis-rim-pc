// What each installed build can show and speak. Text: the build's language archives (game_profile.h). Voices: the
// Japanese cast (Sound/voice) is in every build, the English cast (Sound/voice2) only in the updates that ship it
// (USA and Europe 1.01 and 1.02, Japan 1.04), where the game's own Audio Language option picks between them; the
// launcher only reports it.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "game_profile.h"

namespace tsr {

struct VoiceBank {
    const char* archive;
    const char* cues;   // .acb
    const char* stream; // .awb
};

// The American and European archives.
inline constexpr VoiceBank kJapaneseVoices = {"ROBO_US.CPK", "Sound/voice.acb", "Sound/voice.awb"};
inline constexpr VoiceBank kEnglishVoices = {"ROBO_PATCH_US.CPK", "Sound/voice2.acb", "Sound/voice2.awb"};

// The build's banks: the same files in the archives the build's profile names.
VoiceBank JapaneseVoicesOf(const GameBuild& build);
VoiceBank EnglishVoicesOf(const GameBuild& build);

// Both files of the bank are in the archive's table of contents (the archive is not read further).
bool VoiceBankPresent(const std::filesystem::path& app0, const VoiceBank& bank);

// The build's executable can play either cast and both banks are installed.
bool EnglishVoicesAvailable(const std::filesystem::path& app0, const GameBuild* build);

struct LanguageSupport {
    std::vector<TextLanguage> text;    // never empty; the first is the fallback
    bool english_voices = false; // the game offers its Audio Language option: both banks are installed
};

LanguageSupport LanguagesOf(const std::filesystem::path& app0, const GameBuild* build);

// The language the game shows for a stored preference: the preference when the build has it, else the fallback.
const TextLanguage& EffectiveTextLanguage(const LanguageSupport& support, std::string_view preference);

} // namespace tsr
