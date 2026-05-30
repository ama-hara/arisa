#pragma once

#ifdef _WIN32

#include "arisa/io/async_io.h"
#include <atomic>

#ifndef _WINDOWS_
using HANDLE     = void*;
using ULONG_PTR  = unsigned long long;
using DWORD      = unsigned long;
using BOOL       = int;
#endif

namespace arisa::io {

struct IocpOperation;

class IocpEngine final : public AsyncIoEngine {
public:
    IocpEngine();
    ~IocpEngine() override;

    IocpEngine(const IocpEngine&) = delete;
    IocpEngine& operator=(const IocpEngine&) = delete;

    auto poll(int timeout_ms = -1) -> int override;
    void run() override;
    void stop() override;
    auto is_running() const -> bool override;

    void async_read(void* handle, MutableBuffer buffer, IoCallback callback) override;
    void async_write(void* handle, ConstBuffer buffer, IoCallback callback) override;
    void async_connect(void* handle, std::string_view host, std::uint16_t port, IoCallback callback) override;

private:
    HANDLE              iocp_handle_;
    std::atomic<bool>   running_{ false };
    void handle_completion(DWORD bytes_transferred, ULONG_PTR key, void* overlapped);
};

} // namespace arisa::io

#endif
