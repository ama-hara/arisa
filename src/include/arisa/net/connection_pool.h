#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace arisa::net {

struct PooledConnection {
    std::string host;
    INTERNET_PORT port;
    bool is_https;
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    std::chrono::steady_clock::time_point last_used;
    bool in_use = false;
};

class ConnectionPool {
public:
    ConnectionPool();
    ~ConnectionPool();
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    auto acquire(const std::string& host, INTERNET_PORT port, bool is_https) -> PooledConnection*;
    void release(PooledConnection* conn);
    void cleanup(int max_idle_ms = 30000);
    void clear();
    auto pool_size() const -> std::size_t;
    auto active_count() const -> std::size_t;

private:
    std::vector<std::unique_ptr<PooledConnection>> connections_;
    mutable std::mutex mutex_;
    auto find_available(const std::string& host, INTERNET_PORT port, bool is_https) -> PooledConnection*;
    auto create_connection(const std::string& host, INTERNET_PORT port, bool is_https) -> PooledConnection*;
};

} // namespace arisa::net