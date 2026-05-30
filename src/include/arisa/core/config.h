#pragma once

namespace arisa::config {

// ── 调度器 ──
inline constexpr int    default_max_connections = 8;
inline constexpr int    default_max_retries     = 3;

// ── 分片 ──
inline constexpr int    max_chunks              = 16;
inline constexpr int    min_chunk_size          = 262144;      // 256KB
inline constexpr int    multi_seg_threshold     = 1048576;     // 1MB 以下不分片

// ── 网络 ──
inline constexpr int    connect_timeout_ms      = 30000;
inline constexpr int    read_timeout_ms         = 60000;
inline constexpr int    probe_timeout_ms        = 15000;

// ── I/O ──
inline constexpr int    io_buffer_size          = 262144;      // 256KB
inline constexpr int    iocp_max_events         = 64;

// ── 重试 ──
inline constexpr int    retry_base_delay_s      = 2;
inline constexpr int    retry_max_delay_s       = 10;

// ── 进度 ──
inline constexpr int    control_file_save_interval = 20;       // 每 20 次 tick 保存一次
inline constexpr int    progress_interval_ms    = 100;

// ── 版本 ──
inline constexpr const char* version            = "0.8.0-alpha";
inline constexpr const char* user_agent         = "Arisa/0.4";

} // namespace arisa::config