#include "core/TraceRetentionService.h"

#include <chrono>
#include <iostream>

#include "persistence/SqliteTraceRepository.h"
#include "threadpool/ThreadPool.h"

namespace
{
int64_t SystemNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}

TraceRetentionService::TraceRetentionService(SqliteTraceRepository* repo,
                                             ThreadPool* query_tpool,
                                             Config config,
                                             NowMsFn now_ms_fn)
    : repo_(repo),
      query_tpool_(query_tpool),
      config_(config),
      now_ms_fn_(now_ms_fn ? std::move(now_ms_fn) : NowMsFn(SystemNowMs))
{
}

void TraceRetentionService::TriggerStartupCleanup()
{
    ScheduleCleanupIfNeeded(now_ms_fn_());
}

void TraceRetentionService::TrySchedulePeriodicCleanup(int64_t now_ms)
{
    if (config_.retention_days <= 0 || config_.cleanup_interval_ms <= 0) {
        return;
    }

    const int64_t last_schedule_ms = last_schedule_ms_.load(std::memory_order_acquire);
    if (last_schedule_ms > 0 && now_ms - last_schedule_ms < config_.cleanup_interval_ms) {
        return;
    }
    ScheduleCleanupIfNeeded(now_ms);
}

void TraceRetentionService::ScheduleCleanupIfNeeded(int64_t schedule_now_ms)
{
    if (!repo_ || !query_tpool_ || config_.retention_days <= 0) {
        return;
    }

    bool expected = false;
    if (!cleanup_in_progress_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    // 调度时间在提交成功前就记住，目的是避免“刚提交完 startup cleanup，
    // 下一次周期 tick 立刻又补投一个任务”的重复调度。
    last_schedule_ms_.store(schedule_now_ms, std::memory_order_release);
    auto self = shared_from_this();
    const bool submitted = query_tpool_->submit([self]() {
        self->RunCleanupJob();
    });
    if (!submitted) {
        cleanup_in_progress_.store(false, std::memory_order_release);
    }
}

void TraceRetentionService::RunCleanupJob()
{
    try {
        if (!repo_ || config_.retention_days <= 0) {
            cleanup_in_progress_.store(false, std::memory_order_release);
            return;
        }

        const int64_t now_ms = now_ms_fn_();
        const int64_t cutoff_ms = ComputeCutoffMs(now_ms);
        for (size_t batch_index = 0; batch_index < config_.max_batches_per_run; ++batch_index) {
            const size_t deleted =
                repo_->DeleteExpiredTracesBatch(cutoff_ms, config_.batch_size);
            if (deleted == 0) {
                break;
            }
        }
    } catch (const std::exception& e) {
        std::clog << "[TraceRetention] cleanup failed: " << e.what() << std::endl;
    }
    cleanup_in_progress_.store(false, std::memory_order_release);
}

int64_t TraceRetentionService::ComputeCutoffMs(int64_t now_ms) const
{
    constexpr int64_t kMsPerDay = 24LL * 60 * 60 * 1000;
    return now_ms - static_cast<int64_t>(config_.retention_days) * kMsPerDay;
}
