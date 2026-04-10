#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

class SqliteTraceRepository;
class ThreadPool;

class TraceRetentionService : public std::enable_shared_from_this<TraceRetentionService>
{
public:
    struct Config
    {
        int retention_days = 0;
        size_t batch_size = 500;
        size_t max_batches_per_run = 3;
        int64_t cleanup_interval_ms = 60LL * 60 * 1000;
    };

    using NowMsFn = std::function<int64_t()>;

    TraceRetentionService(SqliteTraceRepository* repo,
                          ThreadPool* query_tpool,
                          Config config,
                          NowMsFn now_ms_fn = {});

    void TriggerStartupCleanup();
    void TrySchedulePeriodicCleanup(int64_t now_ms);

private:
    void ScheduleCleanupIfNeeded(int64_t schedule_now_ms);
    void RunCleanupJob();
    int64_t ComputeCutoffMs(int64_t now_ms) const;

private:
    SqliteTraceRepository* repo_ = nullptr;
    ThreadPool* query_tpool_ = nullptr;
    Config config_;
    NowMsFn now_ms_fn_;
    // retention 是低频后台维护任务，同一时刻只允许跑一个，
    // 否则定时器连续投递时会出现两个清理任务同时抢 SQLite 写锁。
    std::atomic<bool> cleanup_in_progress_{false};
    std::atomic<int64_t> last_schedule_ms_{0};
};
