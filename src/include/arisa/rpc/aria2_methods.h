#pragma once

#include "arisa/rpc/json.h"
#include "arisa/core/engine.h"
#include <functional>
#include <map>
#include <string>
#include <sstream>

namespace arisa::rpc {

using RpcHandler = std::function<json::Value(const json::Value& params)>;
using JInt = json::Int;
using JStr = std::string;
using JArr = json::Array;
using JObj = json::Object;

class Aria2Dispatcher {
public:
    explicit Aria2Dispatcher(Engine& engine) : engine_(engine) {
        register_methods();
    }

    auto dispatch(const std::string& method, const json::Value& params) -> json::Value {
        auto it = handlers_.find(method);
        if (it == handlers_.end()) {
            return JObj{{"code", JInt{1}}, {"message", JStr{"Unknown method: " + method}}};
        }
        try {
            return it->second(params);
        } catch (const std::exception& e) {
            return JObj{{"code", JInt{2}}, {"message", JStr{e.what()}}};
        }
    }

private:
    Engine& engine_;
    std::map<std::string, RpcHandler> handlers_;

    // ── Helpers ──

    auto err(int code, const std::string& msg) -> json::Value {
        return JObj{{"code", JInt{code}}, {"message", JStr{msg}}};
    }

    auto parse_gid(const json::Value& params, std::size_t idx = 0) -> std::optional<TaskId> {
        if (!params.is_array() || params.as_array().size() <= idx) return std::nullopt;
        auto& gid = params[idx];
        if (gid.is_string()) {
            try { return std::stoull(gid.as_string()); }
            catch (...) { return std::nullopt; }
        }
        if (gid.is_int()) return static_cast<TaskId>(gid.as_int());
        return std::nullopt;
    }

    auto opt_params(const json::Value& params, std::size_t idx = 1) -> const json::Object& {
        static const json::Object empty;
        if (params.is_array() && params.as_array().size() > idx && params[idx].is_object())
            return params[idx].as_object();
        return empty;
    }

    auto status_str(TaskStatus s) -> JStr {
        switch (s) {
            case TaskStatus::Pending:     return "waiting";
            case TaskStatus::Connecting:  return "active";
            case TaskStatus::Downloading: return "active";
            case TaskStatus::Paused:      return "paused";
            case TaskStatus::Completed:   return "complete";
            case TaskStatus::Failed:      return "error";
            default: return "unknown";
        }
    }

    auto build_task_status(TaskId id) -> json::Value {
        JObj obj;
        obj["gid"] = JStr{std::to_string(id)};

        auto status = engine_.get_status(id);
        auto* task = engine_.scheduler().get_task(id);

        obj["status"] = status_str(status);
        obj["totalLength"] = JStr{"0"};
        obj["completedLength"] = JStr{"0"};
        obj["uploadLength"] = JStr{"0"};
        obj["uploadSpeed"] = JStr{"0"};
        obj["downloadSpeed"] = JStr{"0"};
        obj["connections"] = JInt{0};
        obj["numSeeders"] = JInt{0};
        obj["seeder"] = JStr{"false"};
        obj["pieceLength"] = JStr{"1048576"};
        obj["numPieces"] = JStr{"1"};
        obj["bitfield"] = JStr{"ff"};

        if (task) {
            auto dl = task->downloaded.load();
            auto total = task->total_size.load();
            auto speed = task->current_speed.load();
            auto conns = task->options.max_connections;

            obj["totalLength"] = JStr{std::to_string(total > 0 ? total : 0)};
            obj["completedLength"] = JStr{std::to_string(dl)};
            obj["downloadSpeed"] = JStr{std::to_string(speed)};
            obj["connections"] = JInt{conns};
            obj["dir"] = task->options.output_path;

            // Files array
            auto filename = task->options.output_filename.empty()
                ? JStr{"download"} : task->options.output_filename;
            auto filepath = task->options.output_path + "/" + filename;

            JArr uris;
            uris.push_back(JObj{{"url", task->options.url}, {"status", JStr{"used"}}});

            JArr files;
            files.push_back(JObj{
                {"index",             JStr{"1"}},
                {"path",              filepath},
                {"length",            JStr{std::to_string(total > 0 ? total : 0)}},
                {"completedLength",   JStr{std::to_string(dl)}},
                {"selected",          JStr{"true"}},
                {"uris",              uris},
            });
            obj["files"] = files;

            if (status == TaskStatus::Completed) {
                obj["errorCode"] = JStr{"0"};
                obj["errorMessage"] = JStr{""};
            } else if (status == TaskStatus::Failed) {
                obj["errorCode"] = JStr{"1"};
                obj["errorMessage"] = task->last_error;
            } else {
                obj["errorCode"] = JStr{"0"};
                obj["errorMessage"] = JStr{""};
            }

            // Options for aria2.getOption
            obj["_options"] = JObj{
                {"dir",                      task->options.output_path},
                {"out",                      filename},
                {"max-connection-per-server", JStr{std::to_string(conns)}},
                {"split",                    JStr{std::to_string(conns)}},
                {"continue",                 JStr{"true"}},
                {"user-agent",               JStr{"Arisa/0.5"}},
            };
        } else {
            obj["status"] = JStr{"removed"};
            obj["errorCode"] = JStr{"0"};
            obj["errorMessage"] = JStr{""};
        }

        return obj;
    }

    auto build_task_list(const std::vector<TaskId>& ids) -> json::Value {
        JArr arr;
        for (auto id : ids) arr.push_back(build_task_status(id));
        return arr;
    }

    // ── Method Registration ──

    void register_methods() {

        // ═══ Version / System ═══

        handlers_["aria2.getVersion"] = [this](const json::Value&) -> json::Value {
            return JObj{
                {"version",         JStr{std::string(Engine::version())}},
                {"enabledFeatures", JArr{
                    JStr{"HTTPS"}, JStr{"HTTP"}, JStr{"Multi-Segment"},
                    JStr{"Resume"}, JStr{"IOCP"}, JStr{"JSON-RPC"}
                }},
            };
        };

        handlers_["aria2.getGlobalStat"] = [this](const json::Value&) -> json::Value {
            auto all = engine_.scheduler().get_all_tasks();
            int active = 0, waiting = 0, stopped = 0;
            Speed total_speed = 0;
            for (auto id : all) {
                auto s = engine_.get_status(id);
                if (s == TaskStatus::Downloading || s == TaskStatus::Connecting) {
                    ++active;
                    auto* t = engine_.scheduler().get_task(id);
                    if (t) total_speed += t->current_speed.load();
                } else if (s == TaskStatus::Pending) {
                    ++waiting;
                } else {
                    ++stopped;
                }
            }
            return JObj{
                {"downloadSpeed", JStr{std::to_string(total_speed)}},
                {"uploadSpeed",   JStr{"0"}},
                {"numActive",     JInt{active}},
                {"numWaiting",    JInt{waiting}},
                {"numStopped",    JInt{stopped}},
                {"numStoppedTotal", JInt{stopped}},
            };
        };

        handlers_["aria2.getGlobalOption"] = [this](const json::Value&) -> json::Value {
            return JObj{
                {"dir",                      JStr{"."}},
                {"max-connection-per-server", JStr{"8"}},
                {"split",                    JStr{"8"}},
                {"continue",                 JStr{"true"}},
                {"user-agent",               JStr{"Arisa/0.5"}},
                {"timeout",                  JStr{"60"}},
                {"connect-timeout",          JStr{"30"}},
                {"max-tries",                JStr{"3"}},
                {"retry-wait",               JStr{"2"}},
            };
        };

        handlers_["aria2.changeGlobalOption"] = [this](const json::Value& params) -> json::Value {
            if (!params.is_array() || params.as_array().empty())
                return err(1, "Missing params");
            // Accept but don't actually change most options yet
            return JStr{"OK"};
        };

        // ═══ Download Management ═══

        handlers_["aria2.addUri"] = [this](const json::Value& params) -> json::Value {
            if (!params.is_array() || params.as_array().empty())
                return err(1, "Missing params");
            auto& uris = params[0].as_array();
            if (uris.empty()) return err(1, "Empty URI list");

            DownloadOptions opts;
            opts.url = uris[0].as_string();

            auto& options = opt_params(params, 1);
            if (options.count("dir"))  opts.output_path = options.at("dir").as_string();
            if (options.count("out"))  opts.output_filename = options.at("out").as_string();
            if (options.count("max-connection-per-server"))
                opts.max_connections = static_cast<int>(options.at("max-connection-per-server").as_int());
            if (options.count("split"))
                opts.max_connections = static_cast<int>(options.at("split").as_int());

            auto result = engine_.add_download(opts);
            if (!result) return err(1, result.error().message);
            return JStr{std::to_string(*result)};
        };

        handlers_["aria2.addTorrent"] = [this](const json::Value&) -> json::Value {
            return err(1, "Torrent not supported in Arisa Engine (no BT)");
        };

        handlers_["aria2.addMetalink"] = [this](const json::Value&) -> json::Value {
            return err(1, "Metalink not yet supported");
        };

        // ═══ Task Control ═══

        handlers_["aria2.remove"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            engine_.cancel(*id);
            return JStr{std::to_string(*id)};
        };

        handlers_["aria2.forceRemove"] = handlers_["aria2.remove"];

        handlers_["aria2.pause"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            engine_.cancel(*id);
            return JStr{std::to_string(*id)};
        };

        handlers_["aria2.forcePause"] = handlers_["aria2.pause"];

        handlers_["aria2.pauseAll"] = [this](const json::Value&) -> json::Value {
            auto all = engine_.scheduler().get_all_tasks();
            for (auto id : all) engine_.cancel(id);
            return JStr{"OK"};
        };

        handlers_["aria2.forcePauseAll"] = handlers_["aria2.pauseAll"];

        handlers_["aria2.unpause"] = [this](const json::Value&) -> json::Value {
            return err(1, "Resume via RPC not yet implemented");
        };

        handlers_["aria2.unpauseAll"] = [this](const json::Value&) -> json::Value {
            return err(1, "Resume via RPC not yet implemented");
        };

        // ═══ Status Query ═══

        handlers_["aria2.tellStatus"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            return build_task_status(*id);
        };

        handlers_["aria2.tellActive"] = [this](const json::Value&) -> json::Value {
            auto ids = engine_.scheduler().get_tasks_by_status(TaskStatus::Downloading);
            auto connecting = engine_.scheduler().get_tasks_by_status(TaskStatus::Connecting);
            ids.insert(ids.end(), connecting.begin(), connecting.end());
            return build_task_list(ids);
        };

        handlers_["aria2.tellWaiting"] = [this](const json::Value& params) -> json::Value {
            auto ids = engine_.scheduler().get_tasks_by_status(TaskStatus::Pending);
            // Support offset/num paging
            if (params.is_array() && params.as_array().size() >= 2) {
                auto offset = static_cast<std::size_t>(params[0].as_int());
                auto num = static_cast<std::size_t>(params[1].as_int());
                if (offset >= ids.size()) return JArr{};
                auto end = std::min(offset + num, ids.size());
                ids = std::vector<TaskId>(ids.begin() + offset, ids.begin() + end);
            }
            return build_task_list(ids);
        };

        handlers_["aria2.tellStopped"] = [this](const json::Value& params) -> json::Value {
            std::vector<TaskId> stopped;
            auto completed = engine_.scheduler().get_tasks_by_status(TaskStatus::Completed);
            auto failed = engine_.scheduler().get_tasks_by_status(TaskStatus::Failed);
            stopped.insert(stopped.end(), completed.begin(), completed.end());
            stopped.insert(stopped.end(), failed.begin(), failed.end());
            // Support offset/num paging
            if (params.is_array() && params.as_array().size() >= 2) {
                auto offset = static_cast<std::size_t>(params[0].as_int());
                auto num = static_cast<std::size_t>(params[1].as_int());
                if (offset >= stopped.size()) return JArr{};
                auto end = std::min(offset + num, stopped.size());
                stopped = std::vector<TaskId>(stopped.begin() + offset, stopped.begin() + end);
            }
            return build_task_list(stopped);
        };

        // ═══ Task Info ═══

        handlers_["aria2.getUris"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            auto* task = engine_.scheduler().get_task(*id);
            if (!task) return JArr{};
            return JArr{JObj{{"url", task->options.url}, {"status", JStr{"used"}}}};
        };

        handlers_["aria2.getFiles"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            auto* task = engine_.scheduler().get_task(*id);
            if (!task) return JArr{};

            auto filename = task->options.output_filename.empty()
                ? JStr{"download"} : task->options.output_filename;
            auto filepath = task->options.output_path + "/" + filename;
            auto dl = task->downloaded.load();
            auto total = task->total_size.load();

            JArr uris;
            uris.push_back(JObj{{"url", task->options.url}, {"status", JStr{"used"}}});

            return JArr{JObj{
                {"index",             JStr{"1"}},
                {"path",              filepath},
                {"length",            JStr{std::to_string(total > 0 ? total : 0)}},
                {"completedLength",   JStr{std::to_string(dl)}},
                {"selected",          JStr{"true"}},
                {"uris",              uris},
            }};
        };

        handlers_["aria2.getServers"] = [this](const json::Value&) -> json::Value {
            return JArr{};
        };

        // ═══ Options ═══

        handlers_["aria2.getOption"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            auto status = build_task_status(*id);
            if (status.is_object() && status.has("_options"))
                return status["_options"];
            return JObj{};
        };

        handlers_["aria2.changeOption"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            // Accept but log, most options not yet dynamically changeable
            return JStr{"OK"};
        };

        // ═══ Results / Cleanup ═══

        handlers_["aria2.purgeDownloadResult"] = [this](const json::Value&) -> json::Value {
            return JStr{"OK"};
        };

        handlers_["aria2.removeDownloadResult"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            return JStr{"OK"};
        };

        handlers_["aria2.changePosition"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            return JStr{std::to_string(*id)};
        };

        handlers_["aria2.changeUri"] = [this](const json::Value& params) -> json::Value {
            auto id = parse_gid(params);
            if (!id) return err(1, "Invalid GID");
            return JArr{JInt{0}, JInt{0}};
        };

        // ═══ Session ═══

        handlers_["aria2.saveSession"] = [this](const json::Value&) -> json::Value {
            return JStr{"OK"};
        };

        // ═══ Shutdown ═══

        handlers_["aria2.shutdown"] = [this](const json::Value&) -> json::Value {
            return JStr{"OK"};
        };

        handlers_["aria2.forceShutdown"] = handlers_["aria2.shutdown"];

        // ═══ System (JSON-RPC spec) ═══

        handlers_["system.multicall"] = [this](const json::Value& params) -> json::Value {
            if (!params.is_array() || params.as_array().empty())
                return err(1, "Missing params");
            auto& calls = params[0].as_array();
            JArr results;
            for (auto& call : calls) {
                auto method = call.get_string("method");
                auto& call_params = call.has("params") ? call["params"] : json::Array{};
                auto result = dispatch(method, call_params);
                if (result.is_object() && result.has("code")) {
                    // Error: wrap in array per multicall spec
                    results.push_back(JArr{result});
                } else {
                    results.push_back(JArr{result});
                }
            }
            return results;
        };

        handlers_["system.listMethods"] = [this](const json::Value&) -> json::Value {
            JArr methods;
            for (auto& [name, _] : handlers_) {
                methods.push_back(JStr{name});
            }
            return methods;
        };

        handlers_["system.listNotifications"] = [this](const json::Value&) -> json::Value {
            return JArr{};
        };
    }
};

} // namespace arisa::rpc