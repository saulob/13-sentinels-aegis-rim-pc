#include "language_support.h"

#include "cpk_patch.h"
#include "game_files.h"

namespace tsr {

bool VoiceBankPresent(const std::filesystem::path& app0, const VoiceBank& bank) {
    CpkArchive archive;
    return archive.Open(app0 / bank.archive) && archive.Find(bank.cues) && archive.Find(bank.stream);
}

bool EnglishVoicesAvailable(const std::filesystem::path& app0, const GameBuild* build) {
    return build && build->english_voices && VoiceBankPresent(app0, kJapaneseVoices) &&
           VoiceBankPresent(app0, kEnglishVoices);
}

LanguageSupport LanguagesOf(const std::filesystem::path& app0, const GameBuild* build) {
    LanguageSupport support;
    support.text = InstalledTextLanguages(app0, build);
    support.english_voices = EnglishVoicesAvailable(app0, build);
    return support;
}

const TextLanguage& EffectiveTextLanguage(const LanguageSupport& support, std::string_view preference) {
    return ResolveTextLanguage(support.text, preference);
}

} // namespace tsr
