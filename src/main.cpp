#include "arisa/core/engine.h"
#include "arisa/core/config.h"
#include "arisa/rpc/rpc_server.h"
#include <print>
#include <thread>
#include <chrono>
#include <string>
#include <memory>

using namespace arisa;

void print_usage() {
    std::println(R"(Usage: ars <url> [options]

Options:
  -o, --output <dir>      Output directory (default: .)
  -n, --name <filename>   Output filename (default: auto from URL)
  -c, --connections <n>   Max connections/chunks (default: 8)
  --rpc-port <port>       Enable aria2-compatible JSON-RPC server

Examples:
  ars https://example.com/file.zip
  ars https://example.com/file.zip -o D:\Downloads -c 8
  ars https://example.com/file.zip --rpc-port 6800

Resume: re-run same command to continue interrupted downloads.
)");
}

auto main(int argc, char* argv[]) -> int {
    std::println(R"(
    +===================================+
    |      Arisa Engine (ars)           |
    |        v0.7.0-alpha               |
    |   "...才不是特意帮你的"            |
    +===================================+
    )");

    if (argc < 2) { print_usage(); return 1; }

    DownloadOptions opts;
    opts.url = argv[1];
    opts.output_path = ".";
    opts.max_connections = 8;
    int rpc_port = 0;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            opts.output_path = argv[++i];
        } else if ((arg == "-n" || arg == "--name") && i + 1 < argc) {
            opts.output_filename = argv[++i];
        } else if ((arg == "-c" || arg == "--connections") && i + 1 < argc) {
            opts.max_connections = std::stoi(argv[++i]);
        } else if (arg == "--rpc-port" && i + 1 < argc) {
            rpc_port = std::stoi(argv[++i]);
        } else {
            std::println("[Error] Unknown: {}", arg);
            print_usage();
            return 1;
        }
    }

    std::println("[ars] URL:         {}", opts.url);
    std::println("[ars] Output:      {}", opts.output_path);
    std::println("[ars] Connections: {}", opts.max_connections);
    if (rpc_port > 0) {
        std::println("[ars] RPC Port:    {}", rpc_port);
    }
    std::println("");

    Engine engine;
    engine.start();

    // RPC server (optional)
    std::unique_ptr<rpc::RpcServer> rpc;
    if (rpc_port > 0) {
        rpc = std::make_unique<rpc::RpcServer>(engine, rpc_port);
        rpc->start();
    }

    engine.set_progress_callback([](
        TaskId, FileOffset downloaded, FileOffset total, Speed speed
    ) {
        auto mb  = downloaded / (1024.0 * 1024.0);
        auto kbs = speed / 1024.0;
        if (total > 0) {
            auto pct = downloaded * 100.0 / total;
            auto total_mb = total / (1024.0 * 1024.0);
            std::print("\r[ars] {:.2f}/{:.2f} MB ({:.0f}%) | {:.1f} KB/s       ",
                mb, total_mb, pct, kbs);
        } else {
            std::print("\r[ars] {:.2f} MB | {:.1f} KB/s       ", mb, kbs);
        }
    });

    auto result = engine.add_download(opts);
    if (!result) {
        std::println("[ars] {}", result.error().message);
        if (rpc) rpc->stop();
        return 1;
    }
    auto id = *result;

    // If RPC enabled, wait indefinitely; otherwise wait for task
    if (rpc) {
        std::println("[ars] RPC server running. Press Ctrl+C to stop.");
        while (true) {
            if (rpc->should_shutdown()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    } else {
        while (true) {
            auto s = engine.get_status(id);
            if (s == TaskStatus::Completed || s == TaskStatus::Failed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::println("");

        auto* task = engine.scheduler().get_task(id);
        if (engine.get_status(id) == TaskStatus::Completed) {
            auto bytes = task ? task->downloaded.load() : 0;
            std::println("[ars] Complete! {} bytes ({:.2f} MB)",
                bytes, bytes / (1024.0 * 1024.0));
        } else {
            std::println("[ars] Failed: {}",
                task ? task->last_error : "unknown");
        }
    }

    if (rpc) rpc->stop();
    return engine.get_status(id) == TaskStatus::Completed ? 0 : 1;
}