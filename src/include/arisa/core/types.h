#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <expected>
#include <chrono>
#include <functional>
#include <vector>
#include <memory>

namespace arisa {

using TaskId = std::uint64_t;
using ByteVector = std::vector<std::uint8_t>;
using Clock      = std::chrono::steady_clock;
using TimePoint  = Clock::time_point;
using Duration   = Clock::duration;
using Speed      = std::uint64_t;
using FileOffset = std::int64_t;

struct Chunk {
    std::size_t  index;
    FileOffset   start;
    FileOffset   end;
    FileOffset   downloaded;
};

enum class ErrorCode {
    Ok = 0,
    ConnectionFailed,
    Timeout,
    DnsResolveFailed,
    TlsHandshakeFailed,
    HttpError,
    FileWriteFailed,
    ChecksumMismatch,
    ResumeFailed,
    Unknown
};

struct Error {
    ErrorCode    code;
    std::string  message;
};

template<typename T>
using Result = std::expected<T, Error>;

inline auto make_error(ErrorCode code, std::string msg) -> Error {
    return { code, std::move(msg) };
}

enum class TaskStatus {
    Pending,
    Connecting,
    Downloading,
    Paused,
    Completed,
    Failed
};

struct DownloadOptions {
    std::string              url;
    std::string              output_path = ".";
    std::string              output_filename;
    int                      max_connections = 4;
    Speed                    max_speed = 0;
    std::vector<std::string> headers;
};

using ProgressCallback = std::function<void(
    TaskId, FileOffset, FileOffset, Speed
)>;

} // namespace arisa