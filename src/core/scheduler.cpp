#include "arisa/core/scheduler.h"
#include "arisa/core/config.h"
#include "arisa/core/resume_manager.h"
#include "arisa/net/http_client.h"
#include <print>
#include <filesystem>
#include <thread>
#include <chrono>
#include <vector>

namespace arisa {

Scheduler::Scheduler(int max_concurrent, int max_retries)
    : max_concurrent_(max_concurrent), max_retries_(max_retries)
{
    std::println("[Scheduler] Initialized (concurrent={}, retries={})",
        max_concurrent, max_retries);
}
Scheduler::~Scheduler() { shutdown(); }

auto Scheduler::submit(DownloadOptions options) -> TaskId {
    std::lock_guard lock(mutex_);
    auto task = std::make_unique<TaskInfo>();
    task->id = next_id_++;
    task->options = std::move(options);
    TaskId id = task->id;
    tasks_.push_back(std::move(task));
    std::println("[Scheduler] Task {} submitted: {}", id, tasks_.back()->options.url);
    try_start_tasks();
    return id;
}
void Scheduler::cancel(TaskId id) {
    std::lock_guard lock(mutex_);
    for (auto& t : tasks_) {
        if (t->id == id) {
            t->cancel_flag.store(true);
            if (t->status.load() == TaskStatus::Pending) {
                t->status.store(TaskStatus::Failed); t->last_error = "Cancelled";
            }
            break;
        }
    }
}
void Scheduler::shutdown() {
    if (shutdown_flag_.exchange(true)) return;
    { std::lock_guard lock(mutex_); for (auto& t : tasks_) t->cancel_flag.store(true); }
    for (auto& t : tasks_) if (t->worker_thread.joinable()) t->worker_thread.join();
}
auto Scheduler::get_status(TaskId id) const -> TaskStatus {
    std::lock_guard lock(mutex_);
    for (auto& t : tasks_) if (t->id == id) return t->status.load();
    return TaskStatus::Failed;
}
auto Scheduler::get_task(TaskId id) -> TaskInfo* {
    std::lock_guard lock(mutex_);
    for (auto& t : tasks_) if (t->id == id) return t.get();
    return nullptr;
}
auto Scheduler::task_count() const -> std::size_t {
    std::lock_guard lock(mutex_); return tasks_.size();
}
void Scheduler::set_progress_callback(ProgressCallback cb) {
    std::lock_guard lock(mutex_); progress_cb_ = std::move(cb);
}
auto Scheduler::running_count() -> int {
    int n = 0;
    for (auto& t : tasks_) {
        auto s = t->status.load();
        if (s == TaskStatus::Connecting || s == TaskStatus::Downloading) ++n;
    }
    return n;
}
void Scheduler::try_start_tasks() {
    if (shutdown_flag_.load()) return;
    for (auto& t : tasks_) {
        if (running_count() >= max_concurrent_) break;
        if (t->status.load() == TaskStatus::Pending && !t->cancel_flag.load()) {
            t->status.store(TaskStatus::Connecting);
            TaskId id = t->id;
            if (t->worker_thread.joinable()) t->worker_thread.join();
            t->worker_thread = std::thread([this, id]() { worker(id); });
        }
    }
}

static auto build_output(const DownloadOptions& opts, std::string& full_path) {
    auto out_dir = opts.output_path;
    auto filename = opts.output_filename;
    if (filename.empty()) {
        auto pos = opts.url.rfind('/');
        if (pos != std::string::npos && pos + 1 < opts.url.size()) {
            auto name = std::string_view(opts.url).substr(pos + 1);
            auto qpos = name.find('?');
            if (qpos != std::string::npos) name = name.substr(0, qpos);
            filename = name.empty() ? "download" : std::string(name);
        } else { filename = "download"; }
    }
    full_path = out_dir + "/" + filename;
    std::filesystem::create_directories(out_dir);
}

void Scheduler::worker(TaskId id) {
    TaskInfo* task = nullptr;
    { std::lock_guard lock(mutex_); for (auto& t : tasks_) if (t->id == id) { task = t.get(); break; } }
    if (!task) return;

    auto& opts = task->options;
    std::string full_path;
    build_output(opts, full_path);
    std::string ctrl_path = control_path_for(full_path);

    task->status.store(TaskStatus::Connecting);
    auto info = net::probe(opts.url);
    bool use_multi = info.has_value() && info->accepts_ranges
                  && info->size > config::multi_seg_threshold;

    if (!use_multi && !info)
        std::println("[Scheduler] Task {} probe failed, single connection", id);

    if (!use_multi) {
        // ©¤©¤ Single connection ©¤©¤
        int attempt = 0, max_tries = max_retries_ + 1;
        Result<FileOffset> result = std::unexpected(make_error(ErrorCode::Unknown, ""));

        while (attempt < max_tries) {
            if (task->cancel_flag.load()) {
                task->status.store(TaskStatus::Failed); task->last_error = "Cancelled"; break;
            }
            if (attempt > 0) {
                int delay = std::min(attempt * config::retry_base_delay_s, config::retry_max_delay_s);
                std::println("[Scheduler] Task {} retry {}/{} in {}s...",
                    id, attempt, max_retries_, delay);
                task->retry_count.store(attempt);
                std::this_thread::sleep_for(std::chrono::seconds(delay));
                task->downloaded.store(0); task->current_speed.store(0);
            }
            task->status.store(attempt == 0 ? TaskStatus::Connecting : TaskStatus::Downloading);
            task->start_time = Clock::now();
            std::println("[Scheduler] Task {} single {} -> {} (attempt {}/{})",
                id, opts.url, full_path, attempt + 1, max_tries);

            result = net::download_file(opts.url, full_path,
                [task, this, id](const uint8_t*, std::size_t, FileOffset dl) -> bool {
                    task->downloaded.store(dl);
                    auto e = Clock::now() - task->start_time;
                    auto s = std::chrono::duration<double>(e).count();
                    if (s > 0.3) task->current_speed.store(static_cast<Speed>(dl / s));
                    ProgressCallback cb;
                    { std::lock_guard lock(mutex_); cb = progress_cb_; }
                    if (cb) cb(id, dl, task->total_size.load(), task->current_speed.load());
                    return !task->cancel_flag.load();
                });
            if (result) break;
            ++attempt;
        }

        if (result) {
            task->downloaded.store(*result); task->status.store(TaskStatus::Completed);
            std::println("[Scheduler] Task {} completed ({} bytes, {} retries)", id, *result, attempt);
        } else {
            task->last_error = result.error().message; task->status.store(TaskStatus::Failed);
            std::println("[Scheduler] Task {} failed: {}", id, task->last_error);
        }

    } else {
        // ©¤©¤ Multi-segment + resume ©¤©¤
        FileOffset file_size = info->size;
        task->total_size.store(file_size);

        int num_chunks = std::min(opts.max_connections, config::max_chunks);
        while (num_chunks > 1 && file_size / num_chunks < config::min_chunk_size)
            num_chunks--;

        auto existing = ControlFile::load(ctrl_path);
        bool resuming = false;

        if (existing.has_value() && existing->file_size == file_size
            && existing->num_chunks == num_chunks && existing->url == opts.url) {
            resuming = true;
            std::println("[Scheduler] Task {} RESUMING from .arisa", id);
            FileOffset already = 0;
            for (auto& c : existing->chunks) already += c.downloaded;
            std::println("[Scheduler]   already: {:.2f} MB", already / (1024.0*1024.0));
        } else {
            std::println("[Scheduler] Task {} multi: {} bytes, {} chunks", id, file_size, num_chunks);
            auto alloc = net::preallocate_file(full_path, file_size);
            if (!alloc) {
                task->last_error = alloc.error().message;
                task->status.store(TaskStatus::Failed);
                std::println("[Scheduler] Task {} prealloc failed: {}", id, task->last_error);
                { std::lock_guard lock(mutex_); try_start_tasks(); }
                return;
            }
        }

        struct ChunkState {
            std::atomic<FileOffset> downloaded{0};
            FileOffset start, end;
            std::atomic<bool> done{false};
            std::atomic<bool> failed{false};
            std::string error;
        };

        std::vector<std::unique_ptr<ChunkState>> chunks;
        FileOffset chunk_size = file_size / num_chunks;
        for (int i = 0; i < num_chunks; ++i) {
            auto c = std::make_unique<ChunkState>();
            c->start = static_cast<FileOffset>(i) * chunk_size;
            c->end = (i == num_chunks - 1) ? (file_size - 1) : (c->start + chunk_size - 1);

            if (resuming && existing->chunks[i].done) {
                c->downloaded.store(existing->chunks[i].end - existing->chunks[i].start + 1);
                c->done.store(true);
                std::println("[Scheduler]   chunk {}: done", i);
            } else if (resuming) {
                c->downloaded.store(existing->chunks[i].downloaded);
                c->start += existing->chunks[i].downloaded;
                std::println("[Scheduler]   chunk {}: resume {:.2f} MB",
                    i, existing->chunks[i].downloaded / (1024.0*1024.0));
            }
            chunks.push_back(std::move(c));
        }

        task->status.store(TaskStatus::Downloading);
        task->start_time = Clock::now();

        ControlFile cf;
        cf.url = opts.url; cf.output_path = full_path;
        cf.file_size = file_size; cf.num_chunks = num_chunks;
        cf.chunks.resize(num_chunks);
        for (int i = 0; i < num_chunks; ++i)
            cf.chunks[i] = { i, chunks[i]->start, chunks[i]->end, 0, chunks[i]->done.load() };
        cf.save(ctrl_path);

        std::vector<std::thread> threads;
        for (int i = 0; i < num_chunks; ++i) {
            if (chunks[i]->done.load()) continue;
            auto* cp = chunks[i].get();
            auto url_s = opts.url;
            auto path_s = full_path;
            auto* cancel = &task->cancel_flag;
            threads.emplace_back([cp, u = std::move(url_s), p = std::move(path_s), cancel]() {
                auto r = net::download_range(u, p, cp->start, cp->end,
                    [cp, cancel](const uint8_t*, std::size_t, FileOffset dl) -> bool {
                        cp->downloaded.store(dl);
                        return !cancel->load();
                    });
                if (r) cp->done.store(true);
                else { cp->failed.store(true); cp->error = r.error().message; }
            });
        }

        int save_tick = 0;
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config::progress_interval_ms));
            FileOffset total = 0;
            bool all_done = true, any_fail = false;
            std::string fail_msg;
            for (int i = 0; i < num_chunks; ++i) {
                total += chunks[i]->downloaded.load();
                if (!chunks[i]->done.load()) all_done = false;
                if (chunks[i]->failed.load()) {
                    any_fail = true;
                    if (fail_msg.empty()) fail_msg = chunks[i]->error;
                }
            }
            task->downloaded.store(total);
            auto elapsed = Clock::now() - task->start_time;
            auto secs = std::chrono::duration<double>(elapsed).count();
            if (secs > 0.3) task->current_speed.store(static_cast<Speed>(total / secs));
            ProgressCallback cb;
            { std::lock_guard lock(mutex_); cb = progress_cb_; }
            if (cb) cb(id, total, file_size, task->current_speed.load());

            if (++save_tick % config::control_file_save_interval == 0) {
                for (int i = 0; i < num_chunks; ++i) {
                    cf.chunks[i].downloaded = chunks[i]->downloaded.load();
                    cf.chunks[i].done = chunks[i]->done.load();
                }
                cf.save(ctrl_path);
            }

            if (task->cancel_flag.load() || all_done || any_fail) break;
        }

        for (auto& t : threads) if (t.joinable()) t.join();

        // Final control file save
        for (int i = 0; i < num_chunks; ++i) {
            cf.chunks[i].downloaded = chunks[i]->downloaded.load();
            cf.chunks[i].done = chunks[i]->done.load();
        }

        bool ok = true;
        std::string last_err;
        for (int i = 0; i < num_chunks; ++i) {
            if (!chunks[i]->done.load()) { ok = false; last_err = chunks[i]->error; }
        }

        if (task->cancel_flag.load()) {
            cf.save(ctrl_path);
            task->last_error = "Cancelled"; task->status.store(TaskStatus::Failed);
            std::println("[Scheduler] Task {} cancelled (progress saved)", id);
        } else if (ok) {
            task->downloaded.store(file_size);
            task->status.store(TaskStatus::Completed);
            std::filesystem::remove(ctrl_path);
            auto secs = std::chrono::duration<double>(Clock::now() - task->start_time).count();
            auto mbs = file_size / (1024.0 * 1024.0) / secs;
            std::println("[Scheduler] Task {} complete: {} bytes, {} chunks, {:.1f} MB/s",
                id, file_size, num_chunks, mbs);
        } else {
            cf.save(ctrl_path);
            task->last_error = last_err; task->status.store(TaskStatus::Failed);
            std::println("[Scheduler] Task {} failed (progress saved): {}", id, last_err);
        }
    }

    { std::lock_guard lock(mutex_); try_start_tasks(); }
}

} // namespace arisa
