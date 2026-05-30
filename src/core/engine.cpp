#include "arisa/core/engine.h"
#include "arisa/core/config.h"
#include <print>

namespace arisa {

Engine::Engine()
    : scheduler_(config::default_max_connections, config::default_max_retries)
{
    std::println("[Arisa] Engine v{} initializing...", version());
}
Engine::~Engine() { stop(); }

void Engine::start() { std::println("[Arisa] Engine started"); }
void Engine::stop() { scheduler_.shutdown(); std::println("[Arisa] Engine stopped"); }

auto Engine::add_download(const DownloadOptions& options) -> Result<TaskId> {
    try { return scheduler_.submit(options); }
    catch (const std::exception& e) {
        return std::unexpected(make_error(ErrorCode::Unknown, e.what()));
    }
}
void Engine::cancel(TaskId id) { scheduler_.cancel(id); }
auto Engine::get_status(TaskId id) -> TaskStatus { return scheduler_.get_status(id); }
void Engine::set_progress_callback(ProgressCallback cb) { scheduler_.set_progress_callback(std::move(cb)); }
auto Engine::scheduler() -> Scheduler& { return scheduler_; }
auto Engine::version() -> std::string_view { return config::version; }

} // namespace arisa
