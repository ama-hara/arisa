#include "arisa/net/connection_pool.h"
#include "arisa/core/config.h"
#include <print>

namespace arisa::net {

ConnectionPool::ConnectionPool() {}
ConnectionPool::~ConnectionPool() { clear(); }

auto ConnectionPool::acquire(const std::string& host, INTERNET_PORT port, bool is_https) -> PooledConnection* {
    std::lock_guard lock(mutex_);
    auto* existing = find_available(host, port, is_https);
    if (existing) {
        existing->in_use = true;
        existing->last_used = std::chrono::steady_clock::now();
        return existing;
    }
    auto* conn = create_connection(host, port, is_https);
    if (conn) {
        conn->in_use = true;
        conn->last_used = std::chrono::steady_clock::now();
    }
    return conn;
}

void ConnectionPool::release(PooledConnection* conn) {
    if (!conn) return;
    std::lock_guard lock(mutex_);
    conn->in_use = false;
    conn->last_used = std::chrono::steady_clock::now();
}

void ConnectionPool::cleanup(int max_idle_ms) {
    std::lock_guard lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto it = connections_.begin();
    while (it != connections_.end()) {
        auto& c = *it;
        if (!c->in_use) {
            auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(now - c->last_used).count();
            if (idle > max_idle_ms) {
                if (c->connect) WinHttpCloseHandle(c->connect);
                if (c->session) WinHttpCloseHandle(c->session);
                it = connections_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void ConnectionPool::clear() {
    std::lock_guard lock(mutex_);
    for (auto& c : connections_) {
        if (c->connect) WinHttpCloseHandle(c->connect);
        if (c->session) WinHttpCloseHandle(c->session);
    }
    connections_.clear();
}

auto ConnectionPool::pool_size() const -> std::size_t {
    std::lock_guard lock(mutex_);
    return connections_.size();
}

auto ConnectionPool::active_count() const -> std::size_t {
    std::lock_guard lock(mutex_);
    std::size_t n = 0;
    for (auto& c : connections_) if (c->in_use) ++n;
    return n;
}

auto ConnectionPool::find_available(const std::string& host, INTERNET_PORT port, bool is_https) -> PooledConnection* {
    for (auto& c : connections_) {
        if (!c->in_use && c->host == host && c->port == port && c->is_https == is_https)
            return c.get();
    }
    return nullptr;
}

auto ConnectionPool::create_connection(const std::string& host, INTERNET_PORT port, bool is_https) -> PooledConnection* {
    std::wstring whost(host.begin(), host.end());
    HINTERNET session = WinHttpOpen(L"Arisa/0.7", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return nullptr;

    WinHttpSetTimeouts(session, config::connect_timeout_ms, config::connect_timeout_ms,
        config::read_timeout_ms, config::read_timeout_ms);

    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    // Enable HTTP/2 (Windows 10 1607+)
    DWORD http2_flag = 0x2;
    WinHttpSetOption(session, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &http2_flag, sizeof(http2_flag));

    HINTERNET conn = WinHttpConnect(session, whost.c_str(), port, 0);
    if (!conn) { WinHttpCloseHandle(session); return nullptr; }

    auto pooled = std::make_unique<PooledConnection>();
    pooled->host = host;
    pooled->port = port;
    pooled->is_https = is_https;
    pooled->session = session;
    pooled->connect = conn;

    auto* ptr = pooled.get();
    connections_.push_back(std::move(pooled));
    return ptr;
}

} // namespace arisa::net