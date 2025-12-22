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
    last_stat_time_ = std::chrono::steady_clock::now();
}

LogBatcher::LogBatcher(MiniMuduo::net::EventLoop *loop, ThreadPool *thread_pool, std::shared_ptr<SqliteLogRepository> repo, std::shared_ptr<AiProvider> ai_client, std::shared_ptr<INotifier> notifier, std::shared_ptr<SqliteConfigRepository> config_repo)
    : loop_(loop), thread_pool_(thread_pool), repo_(repo), ai_client_(ai_client), notifier_(notifier), config_repo_(config_repo)
{
    ring_buffer_.resize(capacity_);
    last_stat_time_ = std::chrono::steady_clock::now();
    loop_->runEvery(0.2, [this]() // 改为 0.2s 轮询，响应更及时
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

    // 1. 处理常规的 dispatch 逻辑
    if (count_ > 0)
    {
        size_t limit = std::min(count_, batch_size_);
        tryDispatchLocked(limit);
    }

    // 2. --- 新增：计算瞬态指标 ---
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stat_time_).count();

    // 每 1000ms (1s) 更新一次仪表盘，配合前端图表精度
    if (elapsed_ms >= 1000) {
        // A. 计算 QPS
        // (当前总处理量 - 上次总处理量) / 时间间隔(秒)
        size_t delta = total_processed_global_ - last_total_processed_;
        double qps = (double)delta * 1000.0 / elapsed_ms;

        // B. 计算背压 (队列占用率)
        double bp = (double)count_ / capacity_;

        // C. 推送到 Repo (更新快照)
        repo_->updateRealtimeMetrics(qps, bp);

        // D. 重置基准点
        last_stat_time_ = now;
        last_total_processed_ = total_processed_global_;
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
    total_processed_global_ += limit; // <--- 记录处理总数

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
        std::string global_summary_text = "No summary available.";
        std::string global_risk = "unknown";
        std::string key_patterns = "[]";

        if (!results_for_summary.empty())
        {
            try
            {
                // 这里 ai_client_->summarize 返回的是 JSON 字符串
                std::string json_str = ai_client_->summarize(results_for_summary, ai_api_key, ai_model, reduce_prompt);

                // 解析 JSON
                try {
                    auto j = nlohmann::json::parse(json_str);
                    global_summary_text = j.value("global_summary", "No summary");
                    // 注意：RiskLevel 是个 Enum，JSON 里是 string
                    global_risk = j.value("global_risk_level", "unknown");
                    // key_patterns 是个数组
                    if (j.contains("key_patterns")) {
                        key_patterns = j["key_patterns"].dump(); // 存成 JSON 字符串
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[LogBatcher] Failed to parse summary JSON: " << e.what() << "\nRaw: " << json_str << std::endl;
                    global_summary_text = "JSON Parse Error: " + std::string(e.what());
                }
            }
            catch (...)
            {
                global_summary_text = "Summary generation failed.";
            }
        }

        // 6. 准备批次统计数据 (DashboardStats)
        // 注意：这里我们只算当前批次的“累加”部分，QPS和Backpressure是瞬态的，这里设为0
        DashboardStats batch_stats;
        batch_stats.total_logs = items.size();

        // 计算批次处理总耗时 (Batch Latency)
        // 逻辑：当前时刻 (Summary 完成) - 批次中最早的任务开始时间
        // 这反映了用户感知的“最大等待时间”
        auto batch_end_time = std::chrono::steady_clock::now();
        int processing_time_ms = 0;

        if (!batch.empty()) {
            auto min_start_time = batch[0].start_time;
            for(const auto& t : batch) {
                if (t.start_time < min_start_time) min_start_time = t.start_time;
            }
            processing_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(batch_end_time - min_start_time).count();
        }

        for(const auto& item : items) {
             nlohmann::json j_risk = item.result.risk_level;
             std::string r = j_risk.get<std::string>();
             if (r == "critical") batch_stats.critical_risk++;
             else if (r == "error") batch_stats.error_risk++;
             else if (r == "warning") batch_stats.warning_risk++;
             else if (r == "info") batch_stats.info_risk++;
             else if (r == "safe") batch_stats.safe_risk++;
             else batch_stats.unknown_risk++;
        }

        // 7. 保存宏观分析结果 (获取 batch_id)
        int64_t batch_id = repo_->saveBatchSummary(
            global_summary_text,
            global_risk,
            key_patterns,
            batch_stats,
            processing_time_ms
        );

        // 8. 保存微观分析结果 (带 batch_id)
        repo_->saveAnalysisResultBatch(items, batch_id);

        // 9. 发送通知 (网络)
        //notifier_->notifyReport(global_summary_text, items);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[LogBatcher] Critical Failure: Batch processing aborted. Reason: " << e.what() << std::endl;
    }
}
