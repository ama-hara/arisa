#pragma once

#include "arisa/core/types.h"
#include <string_view>
#include <cstdint>
#include <functional>

namespace arisa::net {

using DownloadCallback = std::function<bool(
    const uint8_t* data, std::size_t size, FileOffset total_downloaded
)>;

struct FileInfo {
    FileOffset size = -1;
    bool accepts_ranges = false;
};

auto probe(std::string_view url) -> Result<FileInfo>;
auto preallocate_file(const std::string& path, FileOffset size) -> Result<bool>;

auto download_file(
    std::string_view url, std::string_view output_path,
    DownloadCallback on_chunk
) -> Result<FileOffset>;

auto download_range(
    std::string_view url, const std::string& output_path,
    FileOffset range_start, FileOffset range_end,
    DownloadCallback on_chunk
) -> Result<FileOffset>;

} // namespace arisa::net