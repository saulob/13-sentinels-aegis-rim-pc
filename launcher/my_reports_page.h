// The My Reports section of the Bug Report page: the reports sent from this PC, their status and the developer's reply.
// Nothing here asks the service anything until the player presses Refresh or What was sent.
#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

#include "incident_client.h"
#include "report_history.h"
#include "trg/launcher.h"

namespace tsr {

class MyReports {
public:
    explicit MyReports(std::filesystem::path history_file);

    void Draw(trg::Ui& ui, const ServiceConfig& service);
    // False when the report could not be added to the saved history; the report itself was sent.
    bool Remember(const IncidentPackage& package, const ReportReceipt& receipt);
    void BringIntoView() { bring_into_view_ = true; }
    bool HasUpdates() const { return history_.HasUnseen() || history_.HasUnseenMessage(); }
    bool HasNewReply() const { return history_.HasUnseenMessage(); }
    // Applies what finished checks and reads found; called each frame by Draw.
    void Collect();

private:
    // Shared with the thread that asks, which may outlive the launcher window.
    struct Check {
        std::atomic<bool> done{false};
        StatusCheck result;
    };
    struct Read {
        std::atomic<bool> done{false};
        std::string ticket;
        SentReportRead result;
    };

    void Refresh(const ServiceConfig& service);
    void SaveHistory();
    void Open(const std::string& ticket);
    void Collapse();
    void ForgetSentReport();
    void ReadSentReport(const TrackedReport& report, const ServiceConfig& service);
    void DrawHeader(trg::Ui& ui, const ServiceConfig& service);
    void DrawReports(trg::Ui& ui, const ServiceConfig& service);
    void DrawRow(trg::Ui& ui, const TrackedReport& report, size_t index, std::string& toggled);
    void DrawDetails(trg::Ui& ui, const TrackedReport& report, size_t index, const ServiceConfig& service, std::string& toggled,
                     std::string& removed);
    void DrawActions(trg::Ui& ui, const TrackedReport& report, const ServiceConfig& service, std::string& removed);
    void DrawSentReport(trg::Ui& ui);
    bool ConfirmRemoval(trg::Ui& ui);

    ReportHistory history_;
    std::shared_ptr<Check> check_;
    ReportOutcome check_failure_ = ReportOutcome::kSent;
    bool check_failed_ = false;
    bool bring_into_view_ = false;
    bool bring_detail_into_view_ = false;
    std::string open_ticket_;
    // The open report whose developer message was unread when it was opened; keeps its "New reply" cue until it closes.
    std::string fresh_reply_;
    std::shared_ptr<Read> reading_;
    std::string sent_ticket_;
    std::string sent_text_;
    bool sent_gone_ = false;
    bool sent_failed_ = false;
};

} // namespace tsr
