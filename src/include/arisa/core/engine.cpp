#include "arisa/core/engine.h"
#include <print>

namespace arisa {

Engine::Engine()
    : io_engine_(io::create_io_engine())
{
    std::println("[Arisa] Engine v{} initializing...", version());
}

Engine::~Engine() {
    stop();
}

void Engine::start() {
    std::println("[Arisa] Engine started");
    // I/O 引擎在单独线程运行（后面实现）
    // 现在先同步跑
}

void Engine::stop() {
    if (io_engine_ && io_engine_->is_running()) {
        io_engine_->stop();
        std::println("[Arisa] Engine stopped");
    }
}

auto Engine::is_running() const -> bool {
    return io_engine_ && io_engine_->is_running();
}

auto Engine::add_download(const DownloadOptions& options) -> Result<TaskId> {
    // TODO: 创建任务，交给调度器
    std::println("[Arisa] Download requested: {} -> {}",
        options.url, options.output_path);
    return std::unexpected(make_error(
        ErrorCode::Unknown,
        "Scheduler not implemented yet"
    ));
}

void Engine::pause(TaskId /*id*/)   { /* TODO */ }
void Engine::resume(TaskId /*id*/)  { /* TODO */ }
void Engine::cancel(TaskId /*id*/)  { /* TODO */ }

auto Engine::status(TaskId /*id*/) const -> TaskStatus {
    return TaskStatus::Pending;  // TODO
}

void Engine::set_progress_callback(ProgressCallback cb) {
    progress_cb_ = std::move(cb);
}

auto Engine::version() -> std::string_view {
    return "0.1.0-alpha";
}

} // namespace arisa
