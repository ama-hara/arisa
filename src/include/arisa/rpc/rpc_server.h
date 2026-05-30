#pragma once

#include "arisa/rpc/aria2_methods.h"
#include <atomic>
#include <thread>
#include <string>

namespace arisa::rpc {

class RpcServer {
public:
    RpcServer(Engine& engine, int port = 6800);
    ~RpcServer();

    void start();
    void stop();
    auto is_running() const -> bool;
    auto should_shutdown() const -> bool;

private:
    Engine& engine_;
    Aria2Dispatcher dispatcher_;
    int port_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::thread server_thread_;

    void server_loop();
    auto handle_request(const std::string& raw_request) -> std::string;
};

} // namespace arisa::rpc