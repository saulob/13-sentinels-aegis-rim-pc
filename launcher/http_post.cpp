#include "http_post.h"

#include <algorithm>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#ifndef TSR_PORT_VERSION
#define TSR_PORT_VERSION "0"
#endif

namespace tsr {
namespace {

bool IsAddressLiteral(std::string_view text) {
    return text.size() > 2 && std::all_of(text.begin() + 1, text.end() - 1, [](char c) { return c == ':' || c == '.' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });
}

bool IsHostCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

bool ParsePort(std::string_view text, int& port) {
    if (text.empty() || text.size() > 5 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    port = std::stoi(std::string(text));
    return port >= 1 && port <= 65535;
}

} // namespace

bool ParseServiceAddress(std::string_view url, ServiceAddress& address) {
    ServiceAddress parsed;
    if (url.starts_with("https://")) {
        url.remove_prefix(8);
    } else {
        return false;
    }
    const size_t slash = url.find('/');
    std::string_view authority = url.substr(0, slash);
    parsed.path = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));
    if (parsed.path.find_first_of("?#\\ ") != std::string::npos || authority.empty()) return false;
    if (std::any_of(url.begin(), url.end(), [](char c) { return static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) >= 0x7F; })) return false;

    std::string_view host = authority;
    if (authority.front() == '[') {
        const size_t close = authority.find(']');
        if (close == std::string_view::npos) return false;
        host = authority.substr(0, close + 1);
        if (!IsAddressLiteral(host)) return false;
        const std::string_view rest = authority.substr(close + 1);
        if (!rest.empty() && (rest.front() != ':' || !ParsePort(rest.substr(1), parsed.port))) return false;
    } else {
        const size_t colon = authority.find(':');
        host = authority.substr(0, colon);
        if (colon != std::string_view::npos && !ParsePort(authority.substr(colon + 1), parsed.port)) return false;
        if (host.empty() || !std::all_of(host.begin(), host.end(), IsHostCharacter) || host.front() == '.' || host.front() == '-') return false;
    }
    parsed.host = std::string(host);
    address = std::move(parsed);
    return true;
}

#ifdef _WIN32

namespace {

std::wstring Widen(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring wide(size_t(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), size);
    return wide;
}

HttpFailure FailureOf(DWORD error) {
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT: return HttpFailure::kTimedOut;
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
    case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
    case ERROR_WINHTTP_SECURE_INVALID_CA:
    case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
    case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
    case ERROR_WINHTTP_SECURE_INVALID_CERT:
    case ERROR_WINHTTP_SECURE_CERT_REVOKED:
    case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE: return HttpFailure::kCertificate;
    default: return HttpFailure::kUnreachable;
    }
}

class Handle {
public:
    explicit Handle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~Handle() {
        if (handle_) WinHttpCloseHandle(handle_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return handle_ != nullptr; }
    HINTERNET get() const { return handle_; }

private:
    HINTERNET handle_;
};

bool ReadBody(HINTERNET request, const HttpLimits& limits, std::chrono::steady_clock::time_point deadline, HttpReply& reply) {
    for (;;) {
        if (std::chrono::steady_clock::now() > deadline) {
            reply.failure = HttpFailure::kTimedOut;
            return false;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            reply.failure = FailureOf(GetLastError());
            return false;
        }
        if (available == 0) return true;
        if (reply.body.size() + available > limits.most_reply_bytes) {
            reply.failure = HttpFailure::kUnreachable;
            return false;
        }
        const size_t at = reply.body.size();
        reply.body.resize(at + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, reply.body.data() + at, available, &read)) {
            reply.failure = FailureOf(GetLastError());
            return false;
        }
        reply.body.resize(at + read);
    }
}

} // namespace

HttpReply PostJsonOverWinHttp(const std::string& url, const std::string& body, const HttpLimits& limits) {
    HttpReply reply;
    ServiceAddress address;
    if (!ParseServiceAddress(url, address) || body.size() > 0x7FFFFFFF) {
        reply.failure = HttpFailure::kInvalidAddress;
        return reply;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.total_ms);
    const std::wstring agent = Widen(std::string("13-Sentinels-Aegis-Rim-PC/") + TSR_PORT_VERSION + " bug-report");
    Handle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        reply.failure = HttpFailure::kUnreachable;
        return reply;
    }
    WinHttpSetTimeouts(session.get(), limits.connect_ms, limits.connect_ms, limits.send_ms, limits.receive_ms);
#if defined(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2) && defined(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3)
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
#endif
    Handle connection(WinHttpConnect(session.get(), Widen(address.host.front() == '[' ? address.host.substr(1, address.host.size() - 2) : address.host).c_str(),
                                     INTERNET_PORT(address.port), 0));
    if (!connection) {
        reply.failure = FailureOf(GetLastError());
        return reply;
    }
    Handle request(WinHttpOpenRequest(connection.get(), L"POST", Widen(address.path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) {
        reply.failure = FailureOf(GetLastError());
        return reply;
    }
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    const wchar_t headers[] = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    if (!WinHttpSendRequest(request.get(), headers, DWORD(-1), const_cast<char*>(body.data()), DWORD(body.size()), DWORD(body.size()), 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        reply.failure = FailureOf(GetLastError());
        return reply;
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        reply.failure = FailureOf(GetLastError());
        return reply;
    }
    reply.status = int(status);
    ReadBody(request.get(), limits, deadline, reply);
    return reply;
}

#else

HttpReply PostJsonOverWinHttp(const std::string&, const std::string&, const HttpLimits&) {
    HttpReply reply;
    reply.failure = HttpFailure::kUnreachable;
    return reply;
}

#endif

} // namespace tsr
