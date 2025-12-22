#include "core/LogBatcher.h"
#include "LogBatcher.h"
#include "threadpool/ThreadPool.h"
#include "ai/AiProvider.h"
#include "notification/INotifier.h"
#include "persistence/SqliteLogRepository.h"
#include <iostream>
#include "MiniMuduo/net/EventLoop.h"

LogBatcher::LogBatcher(ThreadPool *thread_pool, std::shared_ptr<SqliteLogRepository> repo, std::shared_ptr<AiProvider> ai_client, std::shared_ptr<INotifier> notifier, std::shared_ptr<SqliteConfigRepository> config_repo)
    : thread_pool_(thread_pool), repo_(repo), ai_client_(ai_client), notifier_(notifier), config_repo_(config_repo)
{
    ring_buffer_.resize(capacity_);
}

LogBatcher::LogBatcher(MiniMuduo::net::EventLoop *loop, ThreadPool *thread_pool, std::shared_ptr<SqliteLogRepository> repo, std::shared_ptr<AiProvider> ai_client, std::shared_ptr<INotifier> notifier, std::shared_ptr<SqliteConfigRepository> config_repo)
    : loop_(loop), thread_pool_(thread_pool), repo_(repo), ai_client_(ai_client), notifier_(notifier), config_repo_(config_repo)
{
    ring_buffer_.resize(capacity_);
    loop_->runEvery(0.5, [this]()
                    { this->onTimeout(); });
}

LogBatcher::~LogBatcher()
{
}

bool LogBatcher::push(AnalysisTask &&task)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ >= capacity_)
        return false;
    ring_buffer_[tail_] = std::move(task);
    tail_ = (tail_ + 1) % capacity_;
    count_++;
    if (count_ >= batch_size_)
    {
        tryDispatchLocked(batch_size_);
    }
    return true;
}

// [设计决策] 采用“定期巡检”而非“推迟重置”
// 1. 避免系统调用风暴：高并发下频繁 reset 会导致大量 timerfd_settime 系统调用
//    和用户态/内核态切换，这是严重的性能杀手。
// 2. 负载无关性：runEvery 的开销是固定的（每秒 n 次），不会随 QPS 线性增长，
//    配合 Mutex 的双重检查机制，是目前最稳健的工业级实践。
// 每次只定时，不reset，是因为频繁定时器重置，会频繁系统调用和用户系统态切换，耗时大
// 主要是高负载情况下，可能10ms就满了，到batch_size_了，结果频繁重置定时器，就会导致开销大
// 短负载也有mutex，所以可行，是最靠谱的工业实践
void LogBatcher::onTimeout()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ > 0)
    {
        size_t limit = std::min(count_, batch_size_);
        tryDispatchLocked(limit);
    }
}

void LogBatcher::tryDispatchLocked(size_t limit)
{
    if (thread_pool_->pendingTasks() >= pool_threshold_)
        return;

    std::vector<AnalysisTask> batch;
    batch.reserve(limit);
    for (size_t i = 0; i < limit; i++)
    {
        batch.push_back(std::move(ring_buffer_[head_]));
        head_ = (head_ + 1) % capacity_;
    }
    count_ -= limit;

    // 在聚合批次时刻捕获配置快照
    auto config = config_repo_->getSnapshot();

    thread_pool_->submit([this, batch = std::move(batch), config]() mutable
                         { this->processBatch(std::move(batch), config); });
}

void LogBatcher::processBatch(std::vector<AnalysisTask> &&batch, SystemConfigPtr config)
{
    if (batch.empty())
        return;
    try
    {
        // 1. 准备数据
        std::vector<std::pair<std::string, std::string>> logs;
        logs.reserve(batch.size());

        // 使用快照中的配置
        std::string ai_api_key = config->app_config.ai_api_key;
        std::string ai_model = config->app_config.ai_model;
        std::string map_prompt = config->active_map_prompt;
        std::string reduce_prompt = config->active_reduce_prompt;

        for (const auto &task : batch)
        {
            if (task.trace_id.empty())
            {
                std::cerr << "[Warning] Skipping empty trace_id in batch!" << std::endl;
                continue;
            }
            logs.emplace_back(task.trace_id, task.raw_request_body);
        }

        // 2. 保存原始日志 (IO)
        repo_->saveRawLogBatch(logs);

        // 3. AI Map 阶段分析 (网络)
        std::unordered_map<std::string, LogAnalysisResult> mp;
        try
        {
            mp = ai_client_->analyzeBatch(logs, ai_api_key, ai_model, map_prompt);
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Batcher] AI Analyze Failed: " << e.what() << std::endl;
        }

        // 4. 组装结果
        auto end_time = std::chrono::steady_clock::now();
        std::vector<AnalysisResultItem> items;
        std::vector<LogAnalysisResult> results_for_summary;

        items.reserve(batch.size());
        results_for_summary.reserve(mp.size());

        for (const auto &task : batch)
        {
            auto micro_ms = std::chrono::duration_cast<std::chrono::microseconds>(end_time - task.start_time).count();
            auto it = mp.find(task.trace_id);
            if (it != mp.end())
            {
                items.push_back({task.trace_id, it->second, (int)micro_ms, "SUCCESS"});
                results_for_summary.push_back(it->second);
            }
            else
            {
                LogAnalysisResult error_res;
                error_res.summary = "AI analysis missing";
                error_res.risk_level = RiskLevel::UNKNOWN;
                items.push_back({task.trace_id, error_res, (int)micro_ms, "FAILURE"});
            }
        }

        // 5. AI Reduce 阶段摘要 (网络)
        std::string global_summary = "No summary available.";
        if (!results_for_summary.empty())
        {
            try
            {
                global_summary = ai_client_->summarize(results_for_summary, ai_api_key, ai_model, reduce_prompt);
            }
            catch (...)
            {
                global_summary = "Summary generation failed.";
            }
        }

        // 6. 保存结果 (IO)
        repo_->saveAnalysisResultBatch(items, global_summary);

        // 7. 发送通知 (网络)
        notifier_->notifyReport(global_summary, items);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[LogBatcher] Critical Failure: Batch processing aborted. Reason: " << e.what() << std::endl;
    }
}
