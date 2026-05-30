#pragma once

#include "arisa/rpc/json.h"
#include "arisa/core/engine.h"
#include <functional>
#include <map>
#include <string>

namespace arisa::rpc {

using RpcHandler = std::function<json::Value(const json::Value& params)>;

class Aria2Dispatcher {
public:
    explicit Aria2Dispatcher(Engine& engine) : engine_(engine) {
        register_methods();
    }

    auto dispatch(const std::string& method, const json::Value& params) -> json::Value {
        auto it = handlers_.find(method);
        if (it == handlers_.end()) {
            return json::Object{
                {"code",    Int{1}},
                {"message", String{"Unknown method: " + method}},
            };
        }
        try {
            return it->second(params);
        } catch (const std::exception& e) {
            return json::Object{
                {"code",    Int{2}},
                {"message", String{e.what()}},
            };
        }
    }

private:
    Engine& engine_;
    std::map<std::string, RpcHandler> handlers_;

    void register_methods() {
        handlers_["aria2.getVersion"] = [this](const json::Value&) -> json::Value {
            return json::Object{
                {"version",   std::string(Engine::version())},
                {"enabledFeatures", json::Array{
                    std::string("HTTPS"), std::string("Multi-Segment"),
                    std::string("Resume"), std::string("IOCP")
                }},
            };
        };

        handlers_["aria2.addUri"] = [this](const json::Value& params) -> json::Value {
            if (!params.is_array() || params.as_array().empty())
                return json::Object{{"code", Int{1}}, {"message", "Missing params"}};

            auto& uris = params[0].as_array();
            if (uris.empty())
                return json::Object{{"code", Int{1}}, {"message", "Empty URI list"}};

            DownloadOptions opts;
            opts.url = uris[0].as_string();

            // Parse options dict if present
            if (params.as_array().size() > 1 && params[1].is_object()) {
                auto& options = params[1];
                if (options.has("dir"))
                    opts.output_path = options["dir"].as_string();
                if (options.has("out"))
                    opts.output_filename = options["out"].as_string();
                if (options.has("max-connection-per-server"))
                    opts.max_connections = static_cast<int>(options["max-connection-per-server"].as_int());
                if (options.has("split"))
                    opts.max_connections = static_cast<int>(options["split"].as_int());
            }

            auto result = engine_.add_download(opts);
            if (!result) {
                return json::Object{
                    {"code",    Int{1}},
                    {"message", result.error().message},
                };
            }
            return std::to_string(*result);
        };

        handlers_["aria2.remove"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_task_id(params);
            if (!id) return json::Object{{"code", Int{1}}, {"message", "Invalid GID"}};
            engine_.cancel(*id);
            return std::to_string(*id);
        };

        handlers_["aria2.forceRemove"] = handlers_["aria2.remove"];

        handlers_["aria2.pause"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_task_id(params);
            if (!id) return json::Object{{"code", Int{1}}, {"message", "Invalid GID"}};
            return std::to_string(*id);
        };

        handlers_["aria2.forcePause"] = handlers_["aria2.pause"];

        handlers_["aria2.unpause"] = [this](const json::Value&) -> json::Value {
            return json::Object{{"code", Int{1}}, {"message", "Resume not yet supported via RPC"}};
        };

        handlers_["aria2.tellStatus"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_task_id(params);
            if (!id) return json::Object{{"code", Int{1}}, {"message", "Invalid GID"}};
            return build_task_status(*id);
        };

        handlers_["aria2.tellActive"] = [this](const json::Value&) -> json::Value {
            json::Array arr;
            // iterate active tasks from scheduler
            return arr;
        };

        handlers_["aria2.tellWaiting"] = [this](const json::Value&) -> json::Value {
            return json::Array{};
        };

        handlers_["aria2.tellStopped"] = [this](const json::Value&) -> json::Value {
            return json::Array{};
        };

        handlers_["aria2.getGlobalStat"] = [this](const json::Value&) -> json::Value {
            auto& sched = engine_.scheduler();
            return json::Object{
                {"downloadSpeed", std::string("0")},
                {"uploadSpeed",   std::string("0")},
                {"numActive",     Int{static_cast<Int>(sched.task_count())}},
                {"numWaiting",    Int{0}},
                {"numStopped",    Int{0}},
            };
        };

        handlers_["aria2.shutdown"] = [this](const json::Value&) -> json::Value {
            // Will be handled by the server to trigger shutdown
            return std::string("OK");
        };

        handlers_["aria2.forceShutdown"] = handlers_["aria2.shutdown"];
    }

    auto parse_task_id(const json::Value& params) -> std::optional<TaskId> {
        if (!params.is_array() || params.as_array().empty()) return std::nullopt;
        auto& gid = params[0];
        if (gid.is_string()) {
            try { return std::stoull(gid.as_string()); }
            catch (...) { return std::nullopt; }
        }
        if (gid.is_int()) return static_cast<TaskId>(gid.as_int());
        return std::nullopt;
    }

    auto status_to_aria2(TaskStatus s) -> std::string {
        switch (s) {
            case TaskStatus::Pending:      return "waiting";
            case TaskStatus::Connecting:   return "active";
            case TaskStatus::Downloading:  return "active";
            case TaskStatus::Paused:       return "paused";
            case TaskStatus::Completed:    return "complete";
            case TaskStatus::Failed:       return "error";
            default: return "unknown";
        }
    }

    auto build_task_status(TaskId id) -> json::Value {
        auto status = engine_.get_status(id);
        auto* task = engine_.scheduler().get_task(id);

        json::Object obj;
        obj["gid"] = std::to_string(id);
        obj["status"] = status_to_aria2(status);

        if (task) {
            auto dl = task->downloaded.load();
            auto total = task->total_size.load();
            auto speed = task->current_speed.load();

            obj["totalLength"] = std::to_string(total > 0 ? total : 0);
            obj["completedLength"] = std::to_string(dl);
            obj["downloadSpeed"] = std::to_string(speed);

            if (status == TaskStatus::Failed) {
                obj["errorCode"] = std::string("1");
                obj["errorMessage"] = task->last_error;
            }

            obj["connections"] = Int{task->options.max_connections};

            // URLs
            json::Array url_arr;
            url_arr.push_back(json::Object{{"url", task->options.url}, {"status", std::string("used")}});
            obj["files"] = json::Array{
                json::Object{
                    {"index",     std::string("1")},
                    {"path",      task->options.output_path + "/" + task->options.output_filename},
                    {"length",    std::to_string(total > 0 ? total : 0)},
                    {"completedLength", std::to_string(dl)},
                    {"uris",      url_arr},
                }
            };
        } else {
            obj["status"] = "removed";
        }

        return obj;
    }

    // Alias
    using Int = json::Int;
    using String = std::string;
};

} // namespace arisa::rpc