#pragma once

#include "arisa/core/types.h"
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <string>

namespace arisa {

struct TaskInfo {
    TaskId                  id;
    DownloadOptions         options;

    std::atomic<TaskStatus> status{TaskStatus::Pending};
    std::atomic<FileOffset> downloaded{0};
    std::atomic<FileOffset> total_size{0};
    std::atomic<Speed>      current_speed{0};
    std::atomic<bool>       cancel_flag{false};
    std::atomic<int>        retry_count{0};

    std::string  last_error;
    TimePoint    start_time;
    std::thread  worker_thread;
};

class Scheduler {
public:
    explicit Scheduler(int max_concurrent = 4, int max_retries = 3);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    auto submit(DownloadOptions options) -> TaskId;
    void cancel(TaskId id);
    void shutdown();

    auto get_status(TaskId id) const -> TaskStatus;
    auto get_task(TaskId id) -> TaskInfo*;
    auto task_count() const -> std::size_t;

    void set_progress_callback(ProgressCallback cb);

private:
    std::vector<std::unique_ptr<TaskInfo>> tasks_;
    mutable std::mutex                      mutex_;
    TaskId                                  next_id_{1};
    int                                     max_concurrent_;
    int                                     max_retries_;
    std::atomic<bool>                       shutdown_flag_{false};
    ProgressCallback                        progress_cb_;

    auto running_count() -> int;
    void try_start_tasks();
    void worker(TaskId id);
};

} // namespace arisa