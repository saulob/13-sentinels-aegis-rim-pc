#include "incident_page.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"
#include "game_profile.h"
#include "localization/ui_text.h"
#include "page_margin.h"
#include "trg/platform.h"
#include "utf8.h"

namespace tsr {
namespace {

constexpr float kDescriptionHeight = 112.0f;
constexpr float kPreviewHeight = 300.0f;
constexpr float kAlertTint = 0.16f, kAlertFlashTint = 0.5f;
constexpr float kAlertFlashSeconds = 0.9f;
constexpr char kDot[] = " \xC2\xB7 ";

const char* kPrivacyHeadline = "Nothing is sent automatically.";
const char* kPrivacyNote =
    "The launcher saves a report on this PC, after taking addresses, user and computer names, private folders and "
    "passwords out of the selected log. Automatic filtering cannot promise to catch everything, so check the preview. "
    "You choose whether to attach the saved file to a GitHub issue.";

int KeepDescriptionShort(ImGuiInputTextCallbackData* data) {
    const size_t keep = BytesOfCharacters(std::string_view(data->Buf, size_t(data->BufTextLen)), kMostDescriptionCharacters);
    if (keep < size_t(data->BufTextLen)) data->DeleteChars(int(keep), data->BufTextLen - int(keep));
    return 0;
}

std::string LogDetails(const GameLog& log) {
    return LogDateText(log.written) + kDot + LogSizeText(log.bytes);
}

void AccentText(trg::Ui& ui, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().accent);
    ImGui::TextUnformatted(Tr(text));
    ImGui::PopStyleColor();
}

void LogRoleLine(trg::Ui& ui, LogRole role) {
    const char* text = LogRoleText(role);
    if (!text) return;
    ImGui::PushFont(nullptr, trg::layout::kTextHelp);
    AccentText(ui, text);
    ImGui::PopFont();
}

ImVec4 AlertedColor(const ImVec4& color, const ImVec4& alert, double seconds) {
    const float flash = std::max(0.0f, 1.0f - float(seconds) / kAlertFlashSeconds);
    const float t = kAlertTint + (kAlertFlashTint - kAlertTint) * flash;
    return ImVec4(color.x + (alert.x - color.x) * t, color.y + (alert.y - color.y) * t, color.z + (alert.z - color.z) * t, color.w);
}

void WarningText(trg::Ui& ui, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
    ImGui::TextWrapped("%s", Tr(text));
    ImGui::PopStyleColor();
}

float SemiboldWidth(trg::Ui& ui, const char* text) {
    ImGui::PushFont(ui.fonts().semibold, trg::layout::kTextLabel);
    const float width = ImGui::CalcTextSize(Tr(text)).x;
    ImGui::PopFont();
    return width;
}

void FactLabel(trg::Ui& ui, const char* text) {
    ImGui::PushFont(ui.fonts().semibold, trg::layout::kTextLabel);
    ImGui::TextUnformatted(Tr(text));
    ImGui::PopFont();
}

void FactValue(trg::Ui& ui, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().dim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void DrawFactLine(trg::Ui& ui, float left, float right, float y) {
    const float top = std::floor(y);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, top), ImVec2(right, top + 1.0f), ImGui::GetColorU32(ui.theme().border_row));
}

// The launcher's two-pair grid (Ui::InfoGrid) with the button in its empty second-row cell, flush right like the page's other
// buttons. True when it was pressed.
bool DrawLogFacts(trg::Ui& ui, const GameLog& log) {
    const float scale = ui.scale();
    const float padding = trg::layout::kInfoGridPadding * scale;
    const std::string written = LogDateText(log.written);
    const std::string size = LogSizeText(log.bytes);
    const float left = ImGui::GetCursorScreenPos().x;
    const float room = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, padding));
    if (!ImGui::BeginTable("##selected_log", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoClip)) {
        ImGui::PopStyleVar();
        return false;
    }
    const float weights[4] = {std::max(SemiboldWidth(ui, "Selected log"), SemiboldWidth(ui, "Size")),
                              std::max(ImGui::CalcTextSize(log.name.c_str()).x, ImGui::CalcTextSize(size.c_str()).x),
                              SemiboldWidth(ui, "Last written"), ImGui::CalcTextSize(written.c_str()).x};
    for (const float weight : weights) ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, std::max(weight, 1.0f));
    ImGui::TableNextColumn();
    FactLabel(ui, "Selected log");
    ImGui::TableNextColumn();
    FactValue(ui, log.name);
    ImGui::TableNextColumn();
    FactLabel(ui, "Last written");
    ImGui::TableNextColumn();
    FactValue(ui, written);

    ImGui::TableNextColumn();
    const float second_row = ImGui::GetCursorScreenPos().y - padding;
    ImGui::AlignTextToFramePadding();
    FactLabel(ui, "Size");
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    FactValue(ui, size);
    ImGui::TableNextColumn();
    const ImVec2 cell = ImGui::GetCursorScreenPos();
    const float width = std::min(trg::layout::ControlColumnWidth(room, scale), left + room - cell.x);
    ImGui::SetCursorScreenPos(ImVec2(left + room - width, cell.y));
    const bool pressed = ImGui::Button(Tr("View selected log"), ImVec2(width, 0));
    ImGui::EndTable();
    ImGui::PopStyleVar();
    DrawFactLine(ui, left, left + room, second_row);
    DrawFactLine(ui, left, left + room, ImGui::GetItemRectMax().y);
    return pressed;
}

} // namespace

IncidentPage::IncidentPage(Places places) : places_(std::move(places)) {}

void IncidentPage::Draw(trg::Ui& ui, const ReportFacts& facts) {
    if (!logs_found_) FindLogs();
    if (CheckDraft(description_, !logs_.empty()) != problem_) problem_ = DraftProblem::kNone;
    KeepClearOfScrollbar(std::floor(trg::layout::kContentPadding * ui.scale()));
    AccentText(ui, kPrivacyHeadline);
    ui.Paragraph(kPrivacyNote);
    ui.Heading("1. Report the problem");
    DrawDescription(ui);
    ui.Heading("2. Choose one game log");
    DrawLogs(ui);
    ui.Heading("3. Preview the report");
    DrawPreview(ui, facts);
    ui.Heading("4. Save it");
    DrawSave(ui, facts);
    ui.EndRows();
}

void IncidentPage::FindLogs() {
    logs_ = FindGameLogs(places_.logs_dir, places_.running_log);
    logs_found_ = true;
    selected_ = DefaultGameLog(logs_);
}

void IncidentPage::ForgetPreview() {
    previewed_.reset();
    preview_text_.clear();
    log_failed_ = false;
}

bool IncidentPage::AcceptDraft() {
    problem_ = CheckDraft(description_, !logs_.empty());
    problem_time_ = ImGui::GetTime();
    bring_problem_into_view_ = problem_ != DraftProblem::kNone;
    return problem_ == DraftProblem::kNone;
}

std::shared_ptr<const IncidentPackage> IncidentPage::BuildPackage(const ReportFacts& facts) {
    log_failed_ = false;
    const PrivateFacts private_facts = GatherPrivateFacts(places_.install_folders);
    std::optional<IncidentPackage> package = BuildIncidentPackage(description_, logs_[selected_], facts, private_facts);
    if (!package) {
        log_failed_ = true;
        bring_problem_into_view_ = true;
        FindLogs();
        return nullptr;
    }
    return std::make_shared<const IncidentPackage>(std::move(*package));
}

void IncidentPage::TogglePreview(const ReportFacts& facts) {
    if (previewed_) {
        ForgetPreview();
        return;
    }
    if (!AcceptDraft()) return;
    previewed_ = BuildPackage(facts);
    if (previewed_) preview_text_ = IncidentPreviewText(*previewed_);
}

void IncidentPage::Save(const ReportFacts& facts) {
    if (!AcceptDraft()) return;
    std::shared_ptr<const IncidentPackage> package = previewed_ ? previewed_ : BuildPackage(facts);
    if (!package) return;
    saved_report_ = SaveIncidentReport(*package, places_.logs_dir / "reports");
    save_failed_ = saved_report_.empty();
    bring_result_into_view_ = true;
    if (!save_failed_) {
        description_[0] = '\0';
        ForgetPreview();
    }
}

void IncidentPage::DrawDescription(trg::Ui& ui) {
    const bool too_short = problem_ == DraftProblem::kDescriptionTooShort;
    if (too_short && std::exchange(bring_problem_into_view_, false)) ImGui::SetKeyboardFocusHere();
    const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, too_short ? AlertedColor(frame, ui.theme().warn_dot, ImGui::GetTime() - problem_time_) : frame);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackEdit;
    if (ImGui::InputTextMultiline("##description", description_, sizeof(description_),
                                  ImVec2(-FLT_MIN, kDescriptionHeight * ui.scale()), flags, KeepDescriptionShort)) {
        ForgetPreview();
    }
    ImGui::PopStyleColor();
    const size_t used = CountCharacters(description_);
    const std::string counter = std::to_string(used) + " / " + std::to_string(kMostDescriptionCharacters);
    ImGui::PushFont(nullptr, trg::layout::kTextHelp);
    ImGui::TextColored(used >= kMostDescriptionCharacters ? ui.theme().warn_dot : ui.theme().dim, "%s", counter.c_str());
    if (too_short) {
        ImGui::SameLine();
        ImGui::TextColored(ui.theme().warn_dot, "%s", Tr("Please describe the problem, minimum 10 characters"));
    }
    ImGui::PopFont();
    ui.Help("What were you doing, what went wrong, and does it happen every time?");
}

void IncidentPage::DrawLogs(trg::Ui& ui) {
    if (logs_.empty()) {
        ui.Paragraph("No game log is available yet, so a report cannot be made. Start the game once and try again. With "
                     "Detailed logging off on the About page, only sessions that crashed keep a log.");
        if (problem_ == DraftProblem::kNoLog) {
            if (std::exchange(bring_problem_into_view_, false)) ImGui::SetScrollHereY();
            WarningText(ui, "There is no game log to report.");
        }
        return;
    }
    ui.Help("The newest logs of the game, most recent first. Only the one you choose goes into the report.");
    for (size_t i = 0; i < logs_.size(); ++i) {
        const std::string details = LogDetails(logs_[i]);
        ui.RowLeft(logs_[i].name.c_str(), details.c_str());
        LogRoleLine(ui, logs_[i].role);
        ui.RowControl();
        ImGui::PushID(int(i));
        ImGui::AlignTextToFramePadding();
        if (ImGui::RadioButton(Tr("Use this log"), selected_ == i) && selected_ != i) {
            selected_ = i;
            ForgetPreview();
        }
        ImGui::PopID();
    }
    ui.EndRows();
    if (log_failed_) {
        if (std::exchange(bring_problem_into_view_, false)) ImGui::SetScrollHereY();
        WarningText(ui, "That log could not be read. Choose another one and try again.");
    }
    const GameLog& log = logs_[selected_];
    if (DrawLogFacts(ui, log)) trg::OpenUrl(Utf8(log.path));
}

void IncidentPage::DrawPreview(trg::Ui& ui, const ReportFacts& facts) {
    const bool clicked = ui.ButtonRow("Preview", "Shows exactly what the report holds, after the privacy filter.",
                                      PreviewButtonText(previewed_ != nullptr));
    if (clicked) TogglePreview(facts);
    ui.EndRows();
    if (previewed_) {
        // ReadOnly: ImGui never writes to the text it is given.
        ImGui::InputTextMultiline("##preview", preview_text_.data(), preview_text_.size() + 1,
                                  ImVec2(-FLT_MIN, kPreviewHeight * ui.scale()),
                                  ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
    }
}

void IncidentPage::DrawSave(trg::Ui& ui, const ReportFacts& facts) {
    if (ui.ButtonRow("Save report", "Writes the filtered report to a text file in the logs folder.", "Save report", true)) {
        Save(facts);
    }
    if (ui.ButtonRow("Reports folder", "The saved reports, ready to attach to an issue.", "Open reports folder")) {
        std::error_code ec;
        std::filesystem::create_directories(places_.logs_dir / "reports", ec);
        trg::OpenInFileManager(Utf8(places_.logs_dir / "reports"));
    }
    if (ui.ButtonRow("Report on GitHub", "Opens the issue tracker in your browser. Attach the saved report yourself.", "Open GitHub issues")) {
        trg::OpenUrl(kIssuesUrl);
    }
    ui.EndRows();
    if (save_failed_) {
        ui.Spacer(4);
        WarningText(ui, "The report could not be saved.");
    } else if (!saved_report_.empty()) {
        ui.Spacer(4);
        const std::string name = Utf8(saved_report_.filename());
        ui.StatusCard(trg::Status::kReady, "Report saved", name.c_str());
    }
    if (std::exchange(bring_result_into_view_, false)) ImGui::SetScrollHereY(1.0f);
}

} // namespace tsr
