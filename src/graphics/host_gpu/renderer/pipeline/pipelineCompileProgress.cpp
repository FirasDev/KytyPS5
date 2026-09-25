#include "graphics/host_gpu/renderer/pipeline/pipelineCompileProgress.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

namespace Libs::Graphics::PipelineCompileProgress {

namespace {

std::atomic<uint64_t> g_compiled {0};
std::atomic<uint64_t> g_pending {0};
std::atomic<uint64_t> g_estimated_total {0};
std::atomic<uint64_t> g_first_compile_ms {0};
std::atomic<bool>     g_precompiling {false};

std::mutex              g_phase_mutex;
std::condition_variable g_phase_done;

std::mutex  g_title_mutex;
std::string g_title;

const auto g_start = std::chrono::steady_clock::now();

uint64_t NowMs() {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
	                                 std::chrono::steady_clock::now() - g_start)
	                                 .count());
}

} // namespace

void ReportCompileStarted() {
	g_pending.fetch_add(1, std::memory_order_relaxed);
}

void ReportCompileFinished() {
	g_compiled.fetch_add(1, std::memory_order_relaxed);
	g_pending.fetch_sub(1, std::memory_order_relaxed);

	// Zero means "not started", so a compile landing on millisecond zero borrows millisecond one.
	const auto now   = NowMs();
	uint64_t   unset = 0;
	g_first_compile_ms.compare_exchange_strong(unset, now == 0 ? 1 : now,
	                                           std::memory_order_relaxed);
}

void SetEstimatedTotal(uint64_t total) {
	g_estimated_total.store(total, std::memory_order_relaxed);
}

void SetPrecompiling(bool active) {
	{
		std::scoped_lock lock(g_phase_mutex);
		g_precompiling.store(active, std::memory_order_release);
	}
	g_phase_done.notify_all();
}

void WaitForPrecompile() {
	std::unique_lock lock(g_phase_mutex);
	g_phase_done.wait(lock, [] { return !g_precompiling.load(std::memory_order_acquire); });
}

void SetTitleName(std::string name) {
	std::scoped_lock lock(g_title_mutex);
	if (g_title.empty()) {
		g_title = std::move(name);
	}
}

Snapshot GetSnapshot() {
	const auto first = g_first_compile_ms.load(std::memory_order_relaxed);
	const auto now   = NowMs();

	std::scoped_lock lock(g_title_mutex);
	return {
	    .compiled           = g_compiled.load(std::memory_order_relaxed),
	    .pending            = g_pending.load(std::memory_order_relaxed),
	    .estimated_total    = g_estimated_total.load(std::memory_order_relaxed),
	    .compiling          = g_precompiling.load(std::memory_order_acquire),
	    .compile_elapsed_ms = first == 0 ? 0 : now - first,
	    .title              = g_title,
	};
}

} // namespace Libs::Graphics::PipelineCompileProgress
