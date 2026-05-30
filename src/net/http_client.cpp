#include "arisa/net/http_client.h"

#ifdef _WIN32

#include "arisa/core/config.h"
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include <string>
#include <print>

#pragma comment(lib, "winhttp.lib")

namespace arisa::net {

namespace {

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port;
    bool is_https;
};

auto parse_url(std::string_view url) -> Result<ParsedUrl> {
    std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host_buf[256]{};
    wchar_t path_buf[4096]{};
    uc.lpszHostName = host_buf; uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path_buf; uc.dwUrlPathLength  = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        return std::unexpected(make_error(ErrorCode::HttpError,
            "Bad URL: " + std::string(url)));
    }
    return ParsedUrl{ host_buf, path_buf, uc.nPort,
        uc.nScheme == INTERNET_SCHEME_HTTPS };
}

auto open_session() -> HINTERNET {
    HINTERNET s = WinHttpOpen(
        reinterpret_cast<LPCWSTR>(config::user_agent),
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) {
        WinHttpSetTimeouts(s,
            config::connect_timeout_ms, config::connect_timeout_ms,
            config::read_timeout_ms,    config::read_timeout_ms);
        DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
    }
    return s;
}

auto setup_https(HINTERNET req) {
    DWORD sf = SECURITY_FLAG_IGNORE_UNKNOWN_CA
             | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
             | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
             | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &sf, sizeof(sf));
}

auto download_impl(
    std::string_view url, const std::string& output_path,
    FileOffset range_start, FileOffset range_end,
    DownloadCallback on_chunk
) -> Result<FileOffset>
{
    auto parsed = parse_url(url);
    if (!parsed) return std::unexpected(parsed.error());

    HINTERNET session = open_session();
    if (!session)
        return std::unexpected(make_error(ErrorCode::ConnectionFailed, "WinHttpOpen failed"));

    HINTERNET conn = WinHttpConnect(session, parsed->host.c_str(), parsed->port, 0);
    if (!conn) { WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::ConnectionFailed, "Connect failed")); }

    DWORD flags = parsed->is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", parsed->path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::ConnectionFailed, "OpenRequest failed")); }

    if (parsed->is_https) setup_https(req);

    bool is_range = (range_start >= 0);
    if (is_range) {
        std::wstring rh = L"Range: bytes=" + std::to_wstring(range_start)
                        + L"-" + std::to_wstring(range_end);
        WinHttpAddRequestHeaders(req, rh.c_str(),
            static_cast<DWORD>(rh.length()), WINHTTP_ADDREQ_FLAG_ADD);
    }

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::ConnectionFailed,
            "SendRequest failed: " + std::to_string(err)));
    }

    if (!WinHttpReceiveResponse(req, nullptr)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::HttpError,
            "ReceiveResponse failed: " + std::to_string(err)));
    }

    DWORD status = 0; DWORD ssz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &ssz, WINHTTP_NO_HEADER_INDEX);

    if (is_range && status == 200) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::HttpError, "Range not supported (got 200)"));
    }
    if (status < 200 || status >= 300) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::HttpError, "HTTP " + std::to_string(status)));
    }

    HANDLE hFile;
    if (is_range) {
        hFile = CreateFileA(output_path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER pos; pos.QuadPart = range_start;
            SetFilePointerEx(hFile, pos, NULL, FILE_BEGIN);
        }
    } else {
        hFile = CreateFileA(output_path.c_str(), GENERIC_WRITE,
            0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::FileWriteFailed,
            "Cannot open: " + output_path));
    }

    constexpr DWORD BUF = config::io_buffer_size;
    std::vector<uint8_t> buf(BUF);
    FileOffset total = 0;
    bool cancelled = false;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        DWORD to_read = (avail < BUF) ? avail : BUF;
        DWORD got = 0;
        if (!WinHttpReadData(req, buf.data(), to_read, &got)) break;
        if (got == 0) break;
        DWORD written = 0;
        WriteFile(hFile, buf.data(), got, &written, NULL);
        total += got;
        if (on_chunk && !on_chunk(buf.data(), got, total)) { cancelled = true; break; }
        if (is_range && total >= (range_end - range_start + 1)) break;
    }

    CloseHandle(hFile);
    WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
    if (cancelled) return std::unexpected(make_error(ErrorCode::Unknown, "Cancelled"));
    return total;
}

} // anonymous namespace

auto probe(std::string_view url) -> Result<FileInfo> {
    auto parsed = parse_url(url);
    if (!parsed) return std::unexpected(parsed.error());

    HINTERNET session = open_session();
    if (!session)
        return std::unexpected(make_error(ErrorCode::ConnectionFailed, "WinHttpOpen failed"));
    WinHttpSetTimeouts(session,
        config::probe_timeout_ms, config::probe_timeout_ms,
        config::probe_timeout_ms, config::probe_timeout_ms);

    HINTERNET conn = WinHttpConnect(session, parsed->host.c_str(), parsed->port, 0);
    if (!conn) { WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::ConnectionFailed, "Connect failed")); }

    DWORD flags = parsed->is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"HEAD", parsed->path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::ConnectionFailed, "OpenRequest failed")); }

    if (parsed->is_https) setup_https(req);

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::ConnectionFailed,
            "HEAD send failed: " + std::to_string(err)));
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::HttpError,
            "HEAD recv failed: " + std::to_string(err)));
    }

    DWORD status = 0; DWORD ssz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &ssz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return std::unexpected(make_error(ErrorCode::HttpError,
            "HEAD status " + std::to_string(status)));
    }

    FileInfo info;
    wchar_t cl[64] = {}; DWORD cll = sizeof(cl);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX, cl, &cll, WINHTTP_NO_HEADER_INDEX))
        info.size = std::wcstoll(cl, nullptr, 10);

    DWORD alllen = 0;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &alllen, WINHTTP_NO_HEADER_INDEX);
    if (alllen > 0) {
        std::wstring all(alllen / sizeof(wchar_t) + 1, L'\0');
        WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF,
            WINHTTP_HEADER_NAME_BY_INDEX, &all[0], &alllen, WINHTTP_NO_HEADER_INDEX);
        info.accepts_ranges = (all.find(L"Accept-Ranges: bytes") != std::wstring::npos);
    }

    std::println("[Probe] size={}B ranges={} url={}", info.size, info.accepts_ranges, url);
    WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
    return info;
}

auto preallocate_file(const std::string& path, FileOffset size) -> Result<bool> {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return std::unexpected(make_error(ErrorCode::FileWriteFailed, "Cannot create: " + path));
    LARGE_INTEGER li; li.QuadPart = size;
    SetFilePointerEx(h, li, NULL, FILE_BEGIN);
    SetEndOfFile(h);
    CloseHandle(h);
    return true;
}

auto download_file(std::string_view url, std::string_view output_path,
    DownloadCallback on_chunk) -> Result<FileOffset>
{
    return download_impl(url, std::string(output_path), -1, -1, std::move(on_chunk));
}

auto download_range(std::string_view url, const std::string& output_path,
    FileOffset range_start, FileOffset range_end,
    DownloadCallback on_chunk) -> Result<FileOffset>
{
    return download_impl(url, output_path, range_start, range_end, std::move(on_chunk));
}

} // namespace arisa::net
#endif
