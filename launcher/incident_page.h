#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "incident_report.h"
#include "trg/launcher.h"

namespace tsr {

// The launcher's Bug Report page: report the problem, choose one game log, preview the filtered report, save it. Sending
// is the player's own step: the page opens the issue tracker on request and the saved file is attached by hand.
class IncidentPage {
public:
    struct Places {
        std::filesystem::path logs_dir;
        std::filesystem::path running_log;
        std::filesystem::path user_dir;
        std::vector<std::filesystem::path> install_folders;
    };

    explicit IncidentPage(Places places);

    void Draw(trg::Ui& ui, const ReportFacts& facts);

private:
    void FindLogs();
    void ForgetPreview();
    bool AcceptDraft();
    std::shared_ptr<const IncidentPackage> BuildPackage(const ReportFacts& facts);
    void TogglePreview(const ReportFacts& facts);
    void Save(const ReportFacts& facts);
    void DrawDescription(trg::Ui& ui);
    void DrawLogs(trg::Ui& ui);
    void DrawPreview(trg::Ui& ui, const ReportFacts& facts);
    void DrawSave(trg::Ui& ui, const ReportFacts& facts);

    Places places_;
    char description_[kMostDescriptionCharacters * 4 + 1] = {};
    std::vector<GameLog> logs_;
    bool logs_found_ = false;
    size_t selected_ = 0;
    DraftProblem problem_ = DraftProblem::kNone;
    double problem_time_ = 0.0;
    bool bring_problem_into_view_ = false;
    std::shared_ptr<const IncidentPackage> previewed_;
    std::string preview_text_;
    bool log_failed_ = false;
    std::filesystem::path saved_report_;
    bool save_failed_ = false;
    bool bring_result_into_view_ = false;
};

} // namespace tsr
