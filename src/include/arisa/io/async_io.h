#pragma once

#include "arisa/core/types.h"
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace arisa::io {

using IoCallback = std::function<void(Result<std::size_t>)>;

struct MutableBuffer {
    std::uint8_t* data;
    std::size_t   size;
    auto as_span() -> std::span<std::uint8_t> {
        return std::span<std::uint8_t>(data, size);
    }
};

struct ConstBuffer {
    const std::uint8_t* data;
    std::size_t         size;
    auto as_span() const -> std::span<const std::uint8_t> {
        return std::span<const std::uint8_t>(data, size);
    }
};

class AsyncIoEngine {
public:
    virtual ~AsyncIoEngine() = default;

    virtual auto poll(int timeout_ms = -1) -> int = 0;
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual auto is_running() const -> bool = 0;

    virtual void async_read(void* handle, MutableBuffer buffer, IoCallback callback) = 0;
    virtual void async_write(void* handle, ConstBuffer buffer, IoCallback callback) = 0;
    virtual void async_connect(void* handle, std::string_view host, std::uint16_t port, IoCallback callback) = 0;
};

auto create_io_engine() -> std::unique_ptr<AsyncIoEngine>;

} // namespace arisa::io