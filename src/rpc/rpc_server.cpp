#include "arisa/rpc/rpc_server.h"
#include "arisa/rpc/json.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <print>
#include <thread>

namespace arisa::rpc {

RpcServer::RpcServer(Engine& engine, int port)
    : engine_(engine), dispatcher_(engine), port_(port)
{
}

RpcServer::~RpcServer() { stop(); }

void RpcServer::start() {
    if (running_) return;

#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        initialized = true;
    }
#endif

    server_thread_ = std::thread([this]() { server_loop(); });
}

void RpcServer::stop() {
    running_ = false;
    // Connect to self to unblock accept
    SOCKET unblock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (unblock != INVALID_SOCKET) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port_));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        connect(unblock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#ifdef _WIN32
        closesocket(unblock);
#else
        close(unblock);
#endif
    }
    if (server_thread_.joinable()) server_thread_.join();
}

auto RpcServer::is_running() const -> bool { return running_; }
auto RpcServer::should_shutdown() const -> bool { return shutdown_requested_; }

void RpcServer::server_loop() {
    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        std::println("[ars] RPC Failed to create socket");
        return;
    }

    // Allow reuse
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port_));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::println("[ars] RPC Failed to bind port {}", port_);
#ifdef _WIN32
        closesocket(listen_sock);
#else
        close(listen_sock);
#endif
        return;
    }

    if (listen(listen_sock, 5) == SOCKET_ERROR) {
        std::println("[ars] RPC Failed to listen");
#ifdef _WIN32
        closesocket(listen_sock);
#else
        close(listen_sock);
#endif
        return;
    }

    running_ = true;
    std::println("[ars] RPC Listening on port {} (JSON-RPC 2.0 / aria2 compatible)", port_);

    while (running_) {
        sockaddr_in client_addr{};
        int client_len = sizeof(client_addr);
        SOCKET client = accept(listen_sock,
            reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (!running_) break;
        if (client == INVALID_SOCKET) continue;

        // Read HTTP request
        std::string request;
        char buf[8192];
        int total_read = 0;
        bool headers_done = false;
        std::string header_buf;
        int content_length = 0;

        while (total_read < 65536) {
            int n = recv(client, buf, sizeof(buf), 0);
            if (n <= 0) break;
            request.append(buf, n);
            total_read += n;

            if (!headers_done) {
                auto header_end = request.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    headers_done = true;
                    // Extract Content-Length
                    auto cl_pos = request.find("Content-Length:");
                    if (cl_pos == std::string::npos)
                        cl_pos = request.find("content-length:");
                    if (cl_pos != std::string::npos) {
                        auto val_start = cl_pos + 15;
                        while (val_start < request.size() && request[val_start] == ' ') ++val_start;
                        auto val_end = request.find("\r\n", val_start);
                        content_length = std::stoi(request.substr(val_start, val_end - val_start));
                    }
                    auto body_start = header_end + 4;
                    auto body_so_far = request.size() - body_start;
                    if (static_cast<int>(body_so_far) >= content_length) break;
                }
            } else {
                break;
            }
        }

        // Extract body
        std::string body;
        auto header_end = request.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            body = request.substr(header_end + 4);
        }

        std::string response;
        if (body.empty()) {
            response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        } else {
            response = handle_request(body);
        }

        send(client, response.c_str(), static_cast<int>(response.size()), 0);

#ifdef _WIN32
        closesocket(client);
#else
        close(client);
#endif
    }

#ifdef _WIN32
    closesocket(listen_sock);
#else
    close(listen_sock);
#endif
    std::println("[ars] RPC Server stopped");
}

auto RpcServer::handle_request(const std::string& raw_request) -> std::string {
    // Parse the JSON body
    json::Value parsed;
    try {
        parsed = json::parse(raw_request);
    } catch (const std::exception& e) {
        json::Value err = json::Object{
            {"jsonrpc", std::string("2.0")},
            {"id",      nullptr},
            {"error",   json::Object{
                {"code",    json::Int{-32700}},
                {"message", std::string("Parse error: ") + e.what()},
            }},
        };
        auto body = json::serialize(err);
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
             + std::to_string(body.size()) + "\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + body;
    }

    // Check if batch
    json::Array requests;
    if (parsed.is_array()) {
        requests = parsed.as_array();
    } else {
        requests.push_back(parsed);
    }

    json::Array responses;

    for (auto& req : requests) {
        auto method = req.get_string("method");
        json::Value params = req.has("params") ? req["params"] : json::Array{};
        json::Value id = req.has("id") ? req["id"] : nullptr;

        // Check for shutdown
        if (method == "aria2.shutdown" || method == "aria2.forceShutdown") {
            shutdown_requested_ = true;
        }

        auto result = dispatcher_.dispatch(method, params);

        // Check if result is an error object (has "code" field)
        bool is_error = result.is_object() && result.has("code");

        json::Object resp;
        resp["jsonrpc"] = std::string("2.0");
        resp["id"] = id;

        if (is_error) {
            resp["error"] = result;
        } else {
            resp["result"] = result;
        }

        responses.push_back(resp);
    }

    json::Value response_value;
    if (parsed.is_array()) {
        response_value = responses;
    } else {
        response_value = responses[0];
    }

    auto body = json::serialize(response_value);
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "\r\n" + body;
}

} // namespace arisa::rpc