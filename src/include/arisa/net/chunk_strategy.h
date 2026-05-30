#pragma once
#include "arisa/core/types.h"
#include "arisa/core/config.h"
#include <algorithm>
#include <vector>

namespace arisa::net {

struct NetworkStats {
    Speed bandwidth = 0;
    double rtt = 0.0;
};

struct ChunkPlan {
    int num_chunks;
    FileOffset piece_size;
};

inline auto plan_chunks(FileOffset file_size, int max_connections, const NetworkStats& stats = {}) -> ChunkPlan {
    if (file_size <= 0) return {1, file_size};
    if (file_size <= config::multi_seg_threshold) return {1, file_size};

    int max_c = std::min(max_connections, config::max_chunks);

    if (stats.bandwidth == 0 || stats.rtt <= 0.0) {
        auto target_chunk = static_cast<FileOffset>(12 * 1024 * 1024);
        int n = static_cast<int>(file_size / target_chunk);
        n = std::clamp(n, 1, max_c);
        while (n > 1 && file_size / n < config::min_chunk_size) n--;
        return {n, file_size / n};
    }

    auto bdp = static_cast<FileOffset>(stats.bandwidth * stats.rtt);
    if (bdp < 65536) bdp = 65536;
    int n = static_cast<int>(file_size / bdp);
    n = std::clamp(n, 1, max_c);
    while (n > 1 && file_size / n < config::min_chunk_size) n--;
    return {n, file_size / n};
}

struct ChunkRange {
    FileOffset start;
    FileOffset end;
};

inline auto build_chunks(FileOffset file_size, const ChunkPlan& plan) -> std::vector<ChunkRange> {
    std::vector<ChunkRange> chunks;
    for (int i = 0; i < plan.num_chunks; ++i) {
        FileOffset start = static_cast<FileOffset>(i) * plan.piece_size;
        FileOffset end = (i == plan.num_chunks - 1) ? (file_size - 1) : (start + plan.piece_size - 1);
        chunks.push_back({start, end});
    }
    return chunks;
}

} // namespace arisa::net