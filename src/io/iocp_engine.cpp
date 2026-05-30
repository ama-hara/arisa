#include "arisa/io/iocp_engine.h"

#ifdef _WIN32

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <stdexcept>
#include <print>
#include <string>

namespace arisa::io {

struct IocpOperation {
    OVERLAPPED    overlapped{};
    IoCallback    callback;
    MutableBuffer read_buf{};
    bool          is_read{ false };
};

IocpEngine::IocpEngine() {
    static bool winsock_initialized = false;
    if (!winsock_initialized) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
        winsock_initialized = true;
    }

    iocp_handle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (iocp_handle_ == nullptr) {
        throw std::runtime_error("CreateIoCompletionPort failed");
    }
}

IocpEngine::~IocpEngine() {
    if (iocp_handle_ != nullptr) {
        CloseHandle(iocp_handle_);
    }
}

auto IocpEngine::poll(int timeout_ms) -> int {
    int processed = 0;
    constexpr int MAX_EVENTS = 64;

    for (int i = 0; i < MAX_EVENTS; ++i) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;

        BOOL ok = GetQueuedCompletionStatus(
            iocp_handle_, &bytes, &key, &overlapped,
            (timeout_ms < 0) ? INFINITE : static_cast<DWORD>(timeout_ms)
        );

        if (!ok && overlapped == nullptr) break;

        if (overlapped != nullptr) {
            handle_completion(bytes, key, overlapped);
            ++processed;
        }
    }
    return processed;
}

void IocpEngine::run() {
    running_ = true;
    std::println("[Arisa] IOCP engine started");
    while (running_) { poll(-1); }
}

void IocpEngine::stop() {
    running_ = false;
    PostQueuedCompletionStatus(iocp_handle_, 0, 0, nullptr);
}

auto IocpEngine::is_running() const -> bool {
    return running_;
}

void IocpEngine::async_read(void* handle, MutableBuffer buffer, IoCallback callback) {
    auto* op = new IocpOperation{};
    op->callback = std::move(callback);
    op->read_buf = buffer;
    op->is_read  = true;

    DWORD flags = 0;
    DWORD bytes_read = 0;
    WSABUF wsa_buf{
        .len = static_cast<ULONG>(buffer.size),
        .buf = reinterpret_cast<char*>(buffer.data)
    };

    SOCKET sock = reinterpret_cast<SOCKET>(handle);
    int result = WSARecv(sock, &wsa_buf, 1, &bytes_read, &flags, &op->overlapped, nullptr);

    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            op->callback(std::unexpected(make_error(
                ErrorCode::ConnectionFailed,
                "WSARecv failed: " + std::to_string(err)
            )));
            delete op;
        }
    } else {
        handle_completion(bytes_read, 0, &op->overlapped);
    }
}

void IocpEngine::async_write(void* handle, ConstBuffer buffer, IoCallback callback) {
    auto* op = new IocpOperation{};
    op->callback = std::move(callback);
    op->is_read  = false;

    WSABUF wsa_buf{
        .len = static_cast<ULONG>(buffer.size),
        .buf = const_cast<char*>(reinterpret_cast<const char*>(buffer.data))
    };

    SOCKET sock = reinterpret_cast<SOCKET>(handle);
    DWORD bytes_sent = 0;
    int result = WSASend(sock, &wsa_buf, 1, &bytes_sent, 0, &op->overlapped, nullptr);

    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            op->callback(std::unexpected(make_error(
                ErrorCode::ConnectionFailed,
                "WSASend failed: " + std::to_string(err)
            )));
            delete op;
        }
    } else {
        handle_completion(bytes_sent, 0, &op->overlapped);
    }
}

void IocpEngine::async_connect(void* handle, std::string_view host, std::uint16_t port, IoCallback callback) {
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    std::string host_str(host);
    std::string port_str = std::to_string(port);

    if (getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &result) != 0) {
        callback(std::unexpected(make_error(
            ErrorCode::DnsResolveFailed,
            "DNS resolution failed for: " + host_str
        )));
        return;
    }

    SOCKET sock = reinterpret_cast<SOCKET>(handle);
    int connect_result = connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    if (connect_result == SOCKET_ERROR) {
        callback(std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "connect() failed to " + host_str + ":" + port_str
        )));
    } else {
        callback(std::size_t{0});
    }
}

void IocpEngine::handle_completion(DWORD bytes_transferred, ULONG_PTR, void* overlapped) {
    if (overlapped == nullptr) return;

    auto* op = reinterpret_cast<IocpOperation*>(
        reinterpret_cast<char*>(overlapped) - offsetof(IocpOperation, overlapped)
    );

    if (bytes_transferred == 0) {
        op->callback(std::unexpected(make_error(
            ErrorCode::ConnectionFailed,
            "Connection closed by peer"
        )));
    } else {
        op->callback(static_cast<std::size_t>(bytes_transferred));
    }
    delete op;
}

// ©¤©¤ ¹¤³§º¯Êý ©¤©¤
auto create_io_engine() -> std::unique_ptr<AsyncIoEngine> {
    return std::make_unique<IocpEngine>();
}

} // namespace arisa::io

#endif
