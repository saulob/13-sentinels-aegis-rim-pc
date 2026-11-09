#include "http_post.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
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

bool DecodeChunked(std::string_view body, std::string& out) {
    out.clear();
    for (;;) {
        const size_t line_end = body.find("\r\n");
        if (line_end == std::string_view::npos) return false;
        size_t size = 0;
        size_t digits = 0;
        for (const char c : body.substr(0, line_end)) {
            const int value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (value < 0) break;
            if (++digits > 8) return false;
            size = size * 16 + size_t(value);
        }
        if (digits == 0) return false;
        body.remove_prefix(line_end + 2);
        if (size == 0) return true;
        if (body.size() < size + 2 || body.substr(size, 2) != "\r\n") return false;
        out.append(body.substr(0, size));
        body.remove_prefix(size + 2);
    }
}

std::string Lowercase(std::string_view text) {
    std::string lower(text);
    for (char& c : lower) c = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
    return lower;
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

bool ParseHttpResponse(const std::string& raw, HttpReply& reply, bool* framed) {
    if (framed) *framed = false;
    const size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos || raw.compare(0, 7, "HTTP/1.") != 0 || raw.size() < 12 || raw[8] != ' ') return false;
    reply.body.clear();
    int status = 0;
    for (size_t i = 9; i < 12; ++i) {
        if (raw[i] < '0' || raw[i] > '9') return false;
        status = status * 10 + (raw[i] - '0');
    }
    const std::string headers = Lowercase(std::string_view(raw).substr(0, header_end));
    const std::string_view body = std::string_view(raw).substr(header_end + 4);
    reply.status = status;
    if (headers.find("\r\ntransfer-encoding:") != std::string::npos && headers.find("chunked") != std::string::npos) {
        if (framed) *framed = true;
        return DecodeChunked(body, reply.body);
    }
    if (const size_t at = headers.find("\r\ncontent-length:"); at != std::string::npos) {
        if (framed) *framed = true;
        const size_t length = size_t(std::strtoull(headers.c_str() + at + 17, nullptr, 10));
        if (body.size() < length) return false;
        reply.body.assign(body.substr(0, length));
        return true;
    }
    reply.body.assign(body);
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

HttpReply PostJsonOverHttps(const std::string& url, const std::string& body, const HttpLimits& limits) {
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

namespace {

using Clock = std::chrono::steady_clock;

// The bundled OpenSSL's own certificate folder does not exist on players' systems: the distribution's bundle is used.
constexpr const char* kCaBundles[] = {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                                      "/etc/ssl/ca-bundle.pem", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
                                      "/etc/ssl/cert.pem"};

int MillisecondsUntil(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left <= 0 ? 0 : int(std::min<long long>(left, 0x7FFFFFFF));
}

bool WaitFor(int fd, short events, Clock::time_point deadline) {
    for (;;) {
        pollfd entry{fd, events, 0};
        const int ready = poll(&entry, 1, MillisecondsUntil(deadline));
        if (ready > 0) return true;
        if (ready == 0 || errno != EINTR) return false;
    }
}

// A write to a peer that closed the connection raises SIGPIPE: held back on this thread for the request's lifetime.
class PipeSignalHold {
public:
    PipeSignalHold() {
        sigset_t pipe;
        sigemptyset(&pipe);
        sigaddset(&pipe, SIGPIPE);
        sigset_t pending;
        sigpending(&pending);
        already_pending_ = sigismember(&pending, SIGPIPE) == 1;
        pthread_sigmask(SIG_BLOCK, &pipe, &previous_);
    }
    ~PipeSignalHold() {
        if (!already_pending_) {
            sigset_t pipe;
            sigemptyset(&pipe);
            sigaddset(&pipe, SIGPIPE);
            const timespec none{0, 0};
            while (sigtimedwait(&pipe, nullptr, &none) > 0) {
            }
        }
        pthread_sigmask(SIG_SETMASK, &previous_, nullptr);
    }
    PipeSignalHold(const PipeSignalHold&) = delete;
    PipeSignalHold& operator=(const PipeSignalHold&) = delete;

private:
    sigset_t previous_{};
    bool already_pending_ = false;
};

class Connection {
public:
    ~Connection() {
        if (ssl_) SSL_free(ssl_);
        if (context_) SSL_CTX_free(context_);
        if (fd_ >= 0) close(fd_);
    }

    HttpFailure Open(const ServiceAddress& address, const HttpLimits& limits, Clock::time_point deadline) {
        const std::string host = address.host.front() == '[' ? address.host.substr(1, address.host.size() - 2) : address.host;
        const Clock::time_point connect_by = std::min(deadline, Clock::now() + std::chrono::milliseconds(limits.connect_ms));
        if (HttpFailure failure = Dial(host, address.port, connect_by); failure != HttpFailure::kNone) return failure;

        context_ = SSL_CTX_new(TLS_client_method());
        if (!context_) return HttpFailure::kUnreachable;
        SSL_CTX_set_min_proto_version(context_, TLS1_2_VERSION);
        SSL_CTX_set_options(context_, SSL_OP_IGNORE_UNEXPECTED_EOF);
        SSL_CTX_set_default_verify_paths(context_);
        for (const char* bundle : kCaBundles) {
            if (access(bundle, R_OK) == 0 && SSL_CTX_load_verify_locations(context_, bundle, nullptr) == 1) break;
        }
        SSL_CTX_set_verify(context_, SSL_VERIFY_PEER, nullptr);
        ssl_ = SSL_new(context_);
        if (!ssl_ || SSL_set_fd(ssl_, fd_) != 1) return HttpFailure::kUnreachable;
        X509_VERIFY_PARAM* checks = SSL_get0_param(ssl_);
        X509_VERIFY_PARAM_set_hostflags(checks, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (address.host.front() == '[' ? X509_VERIFY_PARAM_set1_ip_asc(checks, host.c_str()) != 1
                                        : (SSL_set_tlsext_host_name(ssl_, host.c_str()) != 1 ||
                                           X509_VERIFY_PARAM_set1_host(checks, host.c_str(), 0) != 1)) {
            return HttpFailure::kUnreachable;
        }
        for (;;) {
            const int result = SSL_connect(ssl_);
            if (result == 1) return HttpFailure::kNone;
            const int error = SSL_get_error(ssl_, result);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                if (!WaitFor(fd_, error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, connect_by)) return HttpFailure::kTimedOut;
                continue;
            }
            return SSL_get_verify_result(ssl_) != X509_V_OK ? HttpFailure::kCertificate : HttpFailure::kUnreachable;
        }
    }

    HttpFailure Send(const std::string& data, Clock::time_point deadline) {
        for (size_t sent = 0; sent < data.size();) {
            const int chunk = int(std::min<size_t>(data.size() - sent, 1 << 20));
            const int written = SSL_write(ssl_, data.data() + sent, chunk);
            if (written > 0) {
                sent += size_t(written);
                continue;
            }
            const int error = SSL_get_error(ssl_, written);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) return HttpFailure::kUnreachable;
            if (!WaitFor(fd_, error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, deadline)) return HttpFailure::kTimedOut;
        }
        return HttpFailure::kNone;
    }

    // Until `complete` accepts what arrived, or the server closes the connection.
    template <typename Complete>
    HttpFailure Receive(std::string& out, size_t most, Clock::time_point deadline, Complete complete) {
        char buffer[16384];
        for (;;) {
            const int read = SSL_read(ssl_, buffer, int(sizeof(buffer)));
            if (read > 0) {
                if (out.size() + size_t(read) > most) return HttpFailure::kUnreachable;
                out.append(buffer, size_t(read));
                if (complete(out)) return HttpFailure::kNone;
                continue;
            }
            const int error = SSL_get_error(ssl_, read);
            if (error == SSL_ERROR_ZERO_RETURN) return HttpFailure::kNone;
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) return HttpFailure::kUnreachable;
            if (!WaitFor(fd_, error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, deadline)) return HttpFailure::kTimedOut;
        }
    }

private:
    HttpFailure Dial(const std::string& host, int port, Clock::time_point deadline) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* found = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) return HttpFailure::kUnreachable;
        HttpFailure failure = HttpFailure::kUnreachable;
        for (addrinfo* candidate = found; candidate && fd_ < 0; candidate = candidate->ai_next) {
            const int fd = ::socket(candidate->ai_family, candidate->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, candidate->ai_protocol);
            if (fd < 0) continue;
            if (connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0) {
                fd_ = fd;
                break;
            }
            int error = errno;
            if (error == EINPROGRESS) {
                if (!WaitFor(fd, POLLOUT, deadline)) {
                    failure = HttpFailure::kTimedOut;
                    close(fd);
                    continue;
                }
                socklen_t size = sizeof(error);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0) error = errno;
                if (error == 0) {
                    fd_ = fd;
                    break;
                }
            }
            close(fd);
        }
        freeaddrinfo(found);
        return fd_ >= 0 ? HttpFailure::kNone : failure;
    }

    int fd_ = -1;
    SSL_CTX* context_ = nullptr;
    SSL* ssl_ = nullptr;
};


} // namespace

HttpReply PostJsonOverHttps(const std::string& url, const std::string& body, const HttpLimits& limits) {
    HttpReply reply;
    ServiceAddress address;
    if (!ParseServiceAddress(url, address) || body.size() > 0x7FFFFFFF) {
        reply.failure = HttpFailure::kInvalidAddress;
        return reply;
    }
    const PipeSignalHold hold;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(limits.total_ms);
    Connection connection;
    if (const HttpFailure failure = connection.Open(address, limits, deadline); failure != HttpFailure::kNone) {
        reply.failure = failure;
        return reply;
    }
    const std::string host = address.port == 443 ? address.host : address.host + ":" + std::to_string(address.port);
    std::string request = "POST " + address.path + " HTTP/1.1\r\nHost: " + host +
                          "\r\nUser-Agent: 13-Sentinels-Aegis-Rim-PC/" TSR_PORT_VERSION " bug-report"
                          "\r\nContent-Type: application/json\r\nAccept: application/json\r\nContent-Length: " +
                          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
    request += body;
    const Clock::time_point send_by = std::min(deadline, Clock::now() + std::chrono::milliseconds(limits.send_ms));
    if (const HttpFailure failure = connection.Send(request, send_by); failure != HttpFailure::kNone) {
        reply.failure = failure;
        return reply;
    }
    std::string raw;
    const Clock::time_point receive_by = std::min(deadline, Clock::now() + std::chrono::milliseconds(limits.receive_ms));
    const auto complete = [](const std::string& received) {
        HttpReply parsed;
        bool framed = false;
        return ParseHttpResponse(received, parsed, &framed) && framed;
    };
    if (const HttpFailure failure = connection.Receive(raw, limits.most_reply_bytes + 16384, receive_by, complete);
        failure != HttpFailure::kNone) {
        reply.failure = failure;
        return reply;
    }
    if (!ParseHttpResponse(raw, reply) || reply.body.size() > limits.most_reply_bytes) {
        reply = HttpReply{};
        reply.failure = HttpFailure::kUnreachable;
    }
    return reply;
}

#endif

} // namespace tsr
