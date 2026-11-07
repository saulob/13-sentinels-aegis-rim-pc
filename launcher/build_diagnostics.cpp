#include "build_diagnostics.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <system_error>

#include "crypto.h"
#include "game_files.h"
#include "utf8.h"

namespace tsr {
namespace {

namespace fs = std::filesystem;

using Bytes = std::vector<uint8_t>;

constexpr uint64_t kMaxExecutable = 128ull << 20;
constexpr uint64_t kMaxModule = 64ull << 20;
constexpr size_t kMaxSourceFiles = 5000;
constexpr size_t kMaxListedFiles = 60;

// The other releases of the game, so that a package of one is told apart from an unrelated game.
constexpr std::string_view kOtherReleases[] = {"CUSA10602", "CUSA18093", "CUSA19620"};

class FolderSource final : public GameSource {
public:
    explicit FolderSource(fs::path root) : root_(std::move(root)) {}

    std::string Kind() const override { return "game folder"; }

    std::vector<SourceEntry> Files() const override {
        std::vector<SourceEntry> files;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end && files.size() < kMaxSourceFiles; it.increment(ec)) {
            std::error_code item_ec;
            if (!it->is_regular_file(item_ec) || item_ec) {
                continue;
            }
            std::string relative = Utf8(it->path().lexically_relative(root_));
            std::replace(relative.begin(), relative.end(), '\\', '/');
            files.push_back({std::move(relative), it->file_size(item_ec)});
        }
        return files;
    }

    bool Read(const std::string& path, uint64_t max_bytes, Bytes& out) const override {
        out.clear();
        std::error_code ec;
        const fs::path file = LocalPath(path);
        const uint64_t size = fs::file_size(file, ec);
        if (ec || size > max_bytes) {
            return false;
        }
        std::ifstream in(file, std::ios::binary);
        out.resize(static_cast<size_t>(size));
        if (!in || !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size))) {
            out.clear();
            return false;
        }
        return true;
    }

    bool ReadRange(const std::string& path, uint64_t offset, size_t length, Bytes& out) const override {
        out.clear();
        std::ifstream in(LocalPath(path), std::ios::binary);
        out.resize(length);
        if (!in || !in.seekg(static_cast<std::streamoff>(offset)) ||
            !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(length))) {
            out.clear();
            return false;
        }
        return true;
    }

    fs::path LocalPath(const std::string& path) const { return root_ / PathFromUtf8(path); }

private:
    fs::path root_;
};

class PackageSource final : public GameSource {
public:
    explicit PackageSource(Package& package) : package_(package), files_(package.Files()) {}

    std::string Kind() const override { return "package"; }

    std::vector<SourceEntry> Files() const override {
        std::vector<SourceEntry> files;
        for (const PackageFile& file : files_) {
            if (files.size() >= kMaxSourceFiles) {
                break;
            }
            files.push_back({file.path, file.size});
        }
        return files;
    }

    bool Read(const std::string& path, uint64_t max_bytes, Bytes& out) const override {
        return package_.ReadFile(path, max_bytes, out).ok();
    }

    bool ReadRange(const std::string& path, uint64_t offset, size_t length, Bytes& out) const override {
        return package_.ReadFileRange(path, offset, length, out).ok();
    }


private:
    Package& package_;
    std::vector<PackageFile> files_;
};

class Lines {
public:
    __attribute__((format(printf, 2, 3))) void Add(const char* format, ...) {
        va_list args;
        va_start(args, format);
        va_list copy;
        va_copy(copy, args);
        const int length = std::vsnprintf(nullptr, 0, format, copy);
        va_end(copy);
        std::string text(length > 0 ? static_cast<size_t>(length) : 0, '\0');
        if (length > 0) {
            std::vsnprintf(text.data(), text.size() + 1, format, args);
        }
        va_end(args);
        lines_.push_back(std::move(text));
    }
    void Add(const std::string& text) { lines_.push_back(text); }
    std::vector<std::string>& Take() { return lines_; }

private:
    std::vector<std::string> lines_;
};

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Sha256Of(const Bytes& data) {
    uint8_t digest[32];
    if (!crypto::Sha256(data.data(), data.size(), digest)) {
        return {};
    }
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    for (const uint8_t byte : digest) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    return hex;
}

std::string Shown(const std::string& value) {
    return value.empty() ? "(missing)" : value;
}

bool KnownTitleId(const std::string& title_id) {
    const auto supported = SupportedBuilds();
    return std::any_of(supported.begin(), supported.end(), [&](const GameBuild& build) { return title_id == build.title_id; }) ||
           std::find(std::begin(kOtherReleases), std::end(kOtherReleases), title_id) != std::end(kOtherReleases);
}

bool NamesGame(const std::string& sfo) {
    const std::string title = Lower(SfoString(sfo, "TITLE")), content = Lower(SfoString(sfo, "CONTENT_ID"));
    return title.find("13 sentinels") != std::string::npos || content.find("13sentinels") != std::string::npos;
}

void DescribeIdentity(Lines& out, const std::string& sfo, const GameSource& source) {
    out.Add("Game identification:");
    out.Add("  source: %s", source.Kind().c_str());
    for (const char* key : {"TITLE", "TITLE_ID", "CONTENT_ID", "APP_VER", "VERSION", "CATEGORY"}) {
        out.Add("  %s: %s", key, Shown(SfoString(sfo, key)).c_str());
    }
    for (const GameBuild& build : SupportedBuilds()) {
        if (SfoString(sfo, "TITLE_ID") == build.title_id) {
            const PackageLayer& layer = FinishedLayer(build);
            out.Add("  supported build of this TITLE_ID: %s (APP_VER %s, VERSION %s, eboot SHA-256 %s)", build.label,
                    layer.app_ver, layer.sfo_version, build.eboot_sha256);
        }
    }
}

void DescribeExecutables(Lines& out, const GameSource& source) {
    Bytes bytes;
    if (source.Read("eboot.bin", kMaxExecutable, bytes)) {
        out.Add("eboot.bin: %zu bytes, SHA-256 %s", bytes.size(), Shown(Sha256Of(bytes)).c_str());
    } else {
        out.Add("eboot.bin: missing or unreadable");
    }
    for (const char* name : kBundledModules) {
        const std::string path = std::string("sce_module/") + name;
        if (source.Read(path, kMaxModule, bytes)) {
            out.Add("%s: %zu bytes, SHA-256 %s", path.c_str(), bytes.size(), Shown(Sha256Of(bytes)).c_str());
        } else {
            out.Add("%s: missing or unreadable", path.c_str());
        }
    }
}

void DescribeFiles(Lines& out, const std::vector<SourceEntry>& files) {
    out.Add("Files: %zu", files.size());
    for (size_t i = 0; i < files.size() && i < kMaxListedFiles; ++i) {
        out.Add("  %s (%llu bytes)", files[i].path.c_str(), static_cast<unsigned long long>(files[i].size));
    }
}

} // namespace

std::unique_ptr<GameSource> OpenFolderSource(const fs::path& folder) {
    return std::make_unique<FolderSource>(folder);
}

std::unique_ptr<GameSource> OpenPackageSource(Package& package) {
    return std::make_unique<PackageSource>(package);
}

bool ExecutableDiffers(const GameSource& source, const GameBuild& build) {
    Bytes eboot;
    return source.Read("eboot.bin", kMaxExecutable, eboot) && Sha256Of(eboot) != build.eboot_sha256;
}

UnsupportedReport DiagnoseUnsupportedBuild(const GameSource& source, const std::string& sfo) {
    UnsupportedReport report;
    if (!KnownTitleId(SfoString(sfo, "TITLE_ID")) && !NamesGame(sfo)) {
        return report;
    }
    Lines out;
    out.Add(kDiagnosticsBegin);
    out.Add("The game was recognized as 13 Sentinels: Aegis Rim, but this exact build is not supported; nothing was installed.");
    DescribeIdentity(out, sfo, source);
    DescribeExecutables(out, source);
    DescribeFiles(out, source.Files());
    out.Add(kDiagnosticsEnd);
    report.recognized = true;
    report.lines = std::move(out.Take());
    return report;
}

} // namespace tsr
