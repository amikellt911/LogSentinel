#include "core/LogBatcher.h"
#include "LogBatcher.h"
#include "threadpool/ThreadPool.h"
#include "ai/AiProvider.h"
#include "notification/INotifier.h"
#include "persistence/SqliteLogRepository.h"
#include <iostream>
#include "MiniMuduo/net/EventLoop.h"
LogBatcher::LogBatcher(ThreadPool *thread_pool, std::shared_ptr<SqliteLogRepository> repo, std::shared_ptr<AiProvider> ai_client, std::shared_ptr<INotifier> notifier)
    : thread_pool_(thread_pool), repo_(repo), ai_client_(ai_client), notifier_(notifier)
{
    ring_buffer_.resize(capacity_);
}
LogBatcher::LogBatcher(MiniMuduo::net::EventLoop *loop, ThreadPool *thread_pool, std::shared_ptr<SqliteLogRepository> repo, std::shared_ptr<AiProvider> ai_client, std::shared_ptr<INotifier> notifier)
    : loop_(loop), thread_pool_(thread_pool), repo_(repo), ai_client_(ai_client), notifier_(notifier)
{
    ring_buffer_.resize(capacity_);
    loop_->runEvery(0.5, [this]()
                    { this->onTimeout(); });
    // if (!ai_client_) {
    //     std::cerr << "FATAL: AiProvider is null!" << std::endl;
    // } else {
    //     std::cerr << "LogBatcher initialized with AiProvider." << std::endl;
    // }
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
    // 好像不需要锁，因为外面push的时候已经锁了
    // std::lock_guard<std::mutex> lock(mutex_);
    if (thread_pool_->pendingTasks() >= pool_threshold_)
        return;
    std::vector<AnalysisTask> batch;
    batch.reserve(limit);
    for (size_t i = 0; i < limit; i++)
    {
        // emplace_back是直接用参数构造到vector的空间，不需要构造临时对象后再拷贝到vector的空间。
        batch.push_back(std::move(ring_buffer_[head_]));
        head_ = (head_ + 1) % capacity_;
    }
    count_ -= limit;
    thread_pool_->submit([this, batch = std::move(batch)]() mutable
                         { this->processBatch(std::move(batch)); });
}
void LogBatcher::processBatch(std::vector<AnalysisTask> &&batch) // 注意：这里通常传值或引用，如果是 &&，外面要 std::move
{
    // 要不要返回异常？
    if (batch.empty())
        return;
    try
    {
        // 1. 准备数据 (Map 输入)
        std::vector<std::pair<std::string, std::string>> logs;
        logs.reserve(batch.size());
        for (const auto &task : batch)
        {
            // 【新增防御】如果 trace_id 为空，直接丢弃！
            if (task.trace_id.empty())
            {
                std::cerr << "[Warning] Skipping empty trace_id in batch!" << std::endl;
                continue;
            }
            logs.emplace_back(task.trace_id, task.raw_request_body);
        }

        // 2. 原始日志批量落库 (IO)，里面有异常要不要管理异常？
        repo_->saveRawLogBatch(logs);

        // 3. AI 批量分析 (Network - Map Phase)
        // 加上 try-catch，防止 AI 挂了导致整个流程崩溃
        std::unordered_map<std::string, LogAnalysisResult> mp;
        try
        {
            mp = ai_client_->analyzeBatch(logs);
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Batcher] AI Analyze Failed: " << e.what() << std::endl;
            // 继续往下走，生成 Failure 的记录
        }

        // 4. 组装数据 & 准备 Reduce 素材
        auto end_time = std::chrono::steady_clock::now();
        std::vector<AnalysisResultItem> items;
        std::vector<LogAnalysisResult> results_for_summary; // 成功的分析结果

        items.reserve(batch.size());
        results_for_summary.reserve(mp.size());

        for (const auto &task : batch)
        {
            // 计算耗时
            auto micro_ms = std::chrono::duration_cast<std::chrono::microseconds>(end_time - task.start_time).count();

            // 查找结果 (安全性检查)
            auto it = mp.find(task.trace_id);
            if (it != mp.end())
            {
                // 成功：加入 items 和 总结列表
                items.push_back({task.trace_id, it->second, (int)micro_ms, "SUCCESS"});
                results_for_summary.push_back(it->second);
            }
            else
            {
                // 失败：构造一个空的/错误的 Result，状态标记为 FAILURE
                LogAnalysisResult error_res;
                error_res.summary = "AI analysis missing";
                error_res.risk_level = "unknown";
                items.push_back({task.trace_id, error_res, (int)micro_ms, "FAILURE"});
            }
        }

        // 5. AI 宏观总结 (Network - Reduce Phase)
        std::string global_summary = "No summary available.";
        if (!results_for_summary.empty())
        {
            try
            {
                global_summary = ai_client_->summarize(results_for_summary);
            }
            catch (...)
            {
                global_summary = "Summary generation failed.";
            }
        }

        // 6. 结果批量落库 (IO)
        // 注意：这里不用传 global_summary，它只用于通知
        repo_->saveAnalysisResultBatch(items, global_summary);

        // 7. 发送聚合报告 (Network)
        // 这里才用到 global_summary
        notifier_->notifyReport(global_summary, items);
    }
    catch (const std::exception &e)
    {
        // 这里是处理“批次级故障”的最佳地点
        std::cerr << "[LogBatcher] Critical Failure: Batch processing aborted. Reason: " << e.what() << std::endl;
        // 可选：尝试将失败写入死信队列，或者触发报警
    }
}