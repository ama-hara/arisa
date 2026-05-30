#pragma once

#include "arisa/core/types.h"
#include "arisa/core/scheduler.h"
#include <memory>
#include <string_view>

namespace arisa {

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void start();
    void stop();

    auto add_download(const DownloadOptions& options) -> Result<TaskId>;
    void cancel(TaskId id);
    auto get_status(TaskId id) -> TaskStatus;

    void set_progress_callback(ProgressCallback cb);
    auto scheduler() -> Scheduler&;

    static auto version() -> std::string_view;

private:
    Scheduler scheduler_;
};

} // namespace arisa