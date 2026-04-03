#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ai/TraceAiProvider.h"
#define private public
#include "core/TraceSessionManager.h"
#undef private
#include "notification/INotifier.h"
#include "persistence/BufferedTraceRepository.h"
#include "persistence/TraceRepository.h"
#include "threadpool/ThreadPool.h"
#include <nlohmann/json.hpp>

namespace
{
std::unique_ptr<BufferedTraceRepository> MakeBufferedTraceRepository(TraceRepository* repo)
{
    auto sink = std::shared_ptr<TraceRepository>(repo, [](TraceRepository*) {});
    return std::make_unique<BufferedTraceRepository>(std::move(sink));
}

// 用 FakeRepository 记录调用入参，目的是让单测只验证 TraceSessionManager 组装与调用是否正确，
// 不依赖真实 SQLite，从而把失败定位收敛在 manager 逻辑本身。
class FakeTraceRepository : public TraceRepository
{
public:
    std::atomic<bool> save_summary_called{false};
    std::atomic<bool> save_spans_called{false};
    std::atomic<bool> save_analysis_called{false};
    std::atomic<bool> save_prompt_called{false};
    std::atomic<int> save_summary_count{0};
    std::atomic<int> save_spans_count{0};
    std::atomic<int> save_analysis_count{0};
    std::atomic<bool> save_atomic_called{false};
    std::atomic<int> save_atomic_count{0};
    std::vector<std::string> saved_trace_ids;
    mutable std::mutex saved_trace_ids_mutex;

    bool save_atomic_return = true;

    TraceSummary last_summary;
    std::vector<TraceSpanRecord> last_spans;
    std::optional<TraceAnalysisRecord> last_analysis;
    std::optional<PromptDebugRecord> last_prompt_debug;

    bool SaveSingleTraceSummary(const TraceSummary& summary) override
    {
        last_summary = summary;
        save_summary_called.store(true, std::memory_order_release);
        save_summary_count.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    bool SaveSingleTraceSpans(const std::string&,
                              const std::vector<TraceSpanRecord>& spans) override
    {
        last_spans = spans;
        save_spans_called.store(true, std::memory_order_release);
        save_spans_count.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    bool SaveSingleTraceAnalysis(const TraceAnalysisRecord& analysis) override
    {
        last_analysis = analysis;
        save_analysis_called.store(true, std::memory_order_release);
        save_analysis_count.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    bool SaveSinglePromptDebug(const PromptDebugRecord& record) override
    {
        last_prompt_debug = record;
        save_prompt_called.store(true, std::memory_order_release);
        return true;
    }

    bool SavePrimaryBatch(const std::vector<TraceSummary>& summaries,
                          const std::vector<TraceSpanRecord>& spans) override
    {
        if (!summaries.empty()) {
            last_summary = summaries.back();
        }
        last_spans = spans;
        save_summary_called.store(!summaries.empty(), std::memory_order_release);
        save_spans_called.store(!spans.empty(), std::memory_order_release);
        save_summary_count.fetch_add(static_cast<int>(summaries.size()), std::memory_order_acq_rel);
        if (!spans.empty()) {
            save_spans_count.fetch_add(1, std::memory_order_acq_rel);
        }
        save_atomic_called.store(true, std::memory_order_release);
        save_atomic_count.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock(saved_trace_ids_mutex);
            for (const auto& summary : summaries) {
                saved_trace_ids.push_back(summary.trace_id);
            }
        }
        return save_atomic_return;
    }

    bool SaveAnalysisBatch(const std::vector<TraceAnalysisRecord>& analyses,
                           const std::vector<PromptDebugRecord>& prompt_debugs) override
    {
        if (!analyses.empty()) {
            last_analysis = analyses.back();
            save_analysis_called.store(true, std::memory_order_release);
            save_analysis_count.fetch_add(static_cast<int>(analyses.size()), std::memory_order_acq_rel);
        }
        if (!prompt_debugs.empty()) {
            last_prompt_debug = prompt_debugs.back();
            save_prompt_called.store(true, std::memory_order_release);
        }
        return true;
    }

    bool SaveSingleTraceAtomic(const TraceSummary& summary,
                               const std::vector<TraceSpanRecord>& spans,
                               const TraceAnalysisRecord* analysis,
                               const PromptDebugRecord* prompt_debug) override
    {
        last_summary = summary;
        last_spans = spans;
        if (analysis) {
            last_analysis = *analysis;
        } else {
            last_analysis.reset();
        }
        if (prompt_debug) {
            last_prompt_debug = *prompt_debug;
        } else {
            last_prompt_debug.reset();
        }
        save_atomic_called.store(true, std::memory_order_release);
        save_atomic_count.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock(saved_trace_ids_mutex);
            saved_trace_ids.push_back(summary.trace_id);
        }
        return save_atomic_return;
    }
};

// 用 StubAi 固定返回值，目的是稳定覆盖 critical/non-critical 分支，避免单测受网络和模型波动影响。
class StubTraceAi : public TraceAiProvider
{
public:
    LogAnalysisResult result{
        .summary = "stub-summary",
        .risk_level = RiskLevel::UNKNOWN,
        .root_cause = "stub-root-cause",
        .solution = "stub-solution",
    };
    std::atomic<bool> called{false};
    std::string last_payload;

    LogAnalysisResult AnalyzeTrace(const std::string& trace_payload) override
    {
        last_payload = trace_payload;
        called.store(true, std::memory_order_release);
        return result;
    }
};

// 用 SpyNotifier 只记录是否触发通知以及最后一次事件，目的是验证“何时通知”而不是测试 webhook 发送细节。
class SpyNotifier : public INotifier
{
public:
    std::atomic<bool> notify_called{false};
    std::atomic<bool> notify_trace_alert_called{false};
    std::string last_trace_id;
    nlohmann::json last_content;
    TraceAlertEvent last_event;

    void notify(const std::string& trace_id, const nlohmann::json& content) override
    {
        last_trace_id = trace_id;
        last_content = content;
        notify_called.store(true, std::memory_order_release);
    }

    void notifyTraceAlert(const TraceAlertEvent& event) override
    {
        last_event = event;
        notify_trace_alert_called.store(true, std::memory_order_release);
    }
};
} // namespace

class TraceSessionManagerUnitTest : public ::testing::Test
{
protected:
    SpanEvent MakeSpan(size_t trace_key, size_t span_id, int64_t start_time_ms)
    {
        SpanEvent span;
        span.trace_key = trace_key;
        span.span_id = span_id;
        span.start_time_ms = start_time_ms;
        span.name = "placeholder-span";
        span.service_name = "placeholder-service";
        return span;
    }

    bool WaitUntil(const std::function<bool()>& predicate, int timeout_ms = 1000)
    {
        // predicate 就是“条件函数”：当它返回 true 时说明异步状态已满足。
        // 这里统一短轮询，是为了避免每个用例都写重复等待逻辑，也避免固定 sleep 带来的慢和不稳定。
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return predicate();
    }

    bool WaitUntilSessionSatisfies(TraceSessionManager& manager,
                                   size_t trace_key,
                                   const std::function<bool(const TraceSession&)>& predicate,
                                   int timeout_ms = 1000)
    {
        // dispatch 线程改造后，sweep 返回并不等于“session 已经同步回滚完成”。
        // 所以测试里如果要看 ReadyRetryLater / retry_count 这类状态，必须等回滚线程把 session 真正放回 manager。
        return WaitUntil([&manager, trace_key, &predicate]() {
            auto iter = manager.index_by_trace_.find(trace_key);
            if (iter == manager.index_by_trace_.end()) {
                return false;
            }
            if (iter->second >= manager.sessions_.size()) {
                return false;
            }
            if (!manager.sessions_[iter->second]) {
                return false;
            }
            return predicate(*manager.sessions_[iter->second]);
        }, timeout_ms);
    }

    void SweepOneTick(TraceSessionManager& manager,
                      int64_t now_ms,
                      int64_t idle_timeout_ms = 5000,
                      size_t max_dispatch_per_tick = 8)
    {
        manager.SweepExpiredSessions(now_ms, idle_timeout_ms, max_dispatch_per_tick);
    }

    void SweepTraceEndSealWindow(TraceSessionManager& manager,
                                 int64_t idle_timeout_ms = 5000,
                                 size_t max_dispatch_per_tick = 8)
    {
        // 默认 wheel_tick_ms=500。trace_end 的 sealed grace 现在固定为 2 tick，
        // 所以这里推进两次 sweep，等价于“先封口，再给一个短窗口吸收乱序 span，最后才 dispatch”。
        SweepOneTick(manager, /*now_ms*/1000, idle_timeout_ms, max_dispatch_per_tick);
        SweepOneTick(manager, /*now_ms*/1500, idle_timeout_ms, max_dispatch_per_tick);
    }
};

TEST_F(TraceSessionManagerUnitTest, PushSealsWhenTraceEndIsTrueAndDispatchesAfterSealDelay)
{
    // 目的：验证 trace_end=true 时不会立刻 dispatch，而是先进入 sealed，再在固定短窗口后分发。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(11, 101, 1000);
    span.trace_end = true;

    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.size(), 1u);
    auto iter = manager.index_by_trace_.find(11);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state, TraceSession::LifecycleState::Sealed);
    EXPECT_EQ(manager.sessions_[iter->second]->sealed_deadline_tick, 2u);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));

    SweepOneTick(manager, /*now_ms*/1000);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));
    EXPECT_EQ(manager.size(), 1u);

    SweepOneTick(manager, /*now_ms*/1500);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_EQ(repo.last_summary.trace_id, "11");
    EXPECT_EQ(repo.last_spans.size(), 1u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, RuntimeStatsTrackDispatchWorkerAiEnqueueAndBufferedFlush)
{
    // 目的：验证埋点现在能把两段时间线分清：
    // 1) manager worker 内的 AI + analysis enqueue
    // 2) BufferedTraceRepository 后台线程里的真实 batch flush
    ThreadPool pool(1);
    FakeTraceRepository repo;
    StubTraceAi ai;
    BufferedTraceRepository::Config buffer_config;
    buffer_config.primary_flush_interval_ms = 20;
    buffer_config.analysis_flush_interval_ms = 20;
    auto sink = std::shared_ptr<TraceRepository>(&repo, [](TraceRepository*) {});
    auto buffered_repo = std::make_unique<BufferedTraceRepository>(std::move(sink), buffer_config);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(910, 91001, 1000);
    span.trace_end = true;

    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntil([&repo]() {
        return repo.save_summary_called.load(std::memory_order_acquire) &&
               repo.save_spans_called.load(std::memory_order_acquire) &&
               repo.save_analysis_called.load(std::memory_order_acquire);
    }, 1000));

    const TraceSessionManager::RuntimeStatsSnapshot stats = manager.SnapshotRuntimeStats();
    EXPECT_EQ(stats.dispatch_count, 1u);
    EXPECT_EQ(stats.submit_ok_count, 1u);
    EXPECT_EQ(stats.submit_fail_count, 0u);
    EXPECT_EQ(stats.worker_begin_count, 1u);
    EXPECT_EQ(stats.worker_done_count, 1u);
    EXPECT_EQ(stats.ai_calls, 1u);
    EXPECT_GE(stats.ai_total_ns, 1u);
    EXPECT_EQ(stats.analysis_enqueue_calls, 1u);
    EXPECT_GE(stats.analysis_enqueue_total_ns, 1u);

    const BufferedTraceRepository::RuntimeStatsSnapshot buffered_stats = buffered_repo->SnapshotRuntimeStats();
    EXPECT_EQ(buffered_stats.primary_append_calls, 1u);
    EXPECT_EQ(buffered_stats.analysis_append_calls, 1u);
    EXPECT_EQ(buffered_stats.primary_flush_calls, 1u);
    EXPECT_EQ(buffered_stats.analysis_flush_calls, 1u);
    EXPECT_EQ(buffered_stats.primary_flushed_summary_count, 1u);
    EXPECT_EQ(buffered_stats.primary_flushed_span_count, 1u);
    EXPECT_EQ(buffered_stats.analysis_flushed_analysis_count, 1u);
    EXPECT_GE(buffered_stats.primary_flush_total_ns, 1u);
    EXPECT_GE(buffered_stats.analysis_flush_total_ns, 1u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, SealedSessionAcceptsLateSpanWithoutRenewingDeadline)
{
    // 目的：验证 sealed 短窗口内的新 span 可以并入当前 session，但 sealed_deadline_tick 不会被向后续命。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent first = MakeSpan(912, 91201, 1000);
    first.trace_end = true;
    SpanEvent late = MakeSpan(912, 91202, 1100);

    ASSERT_EQ(manager.Push(first), TraceSessionManager::PushResult::Accepted);
    auto iter = manager.index_by_trace_.find(912);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->sealed_deadline_tick, 2u);

    ASSERT_EQ(manager.Push(late), TraceSessionManager::PushResult::Accepted);
    iter = manager.index_by_trace_.find(912);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state, TraceSession::LifecycleState::Sealed);
    EXPECT_EQ(manager.sessions_[iter->second]->sealed_deadline_tick, 2u);
    EXPECT_EQ(manager.sessions_[iter->second]->spans.size(), 2u);

    SweepOneTick(manager, /*now_ms*/1000);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));

    SweepOneTick(manager, /*now_ms*/1500);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    EXPECT_EQ(repo.last_spans.size(), 2u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, PushSealsWhenCapacityReachedAndDispatchesAfterOneTick)
{
    // 目的：验证容量打满后先进入 sealed，再在最短 1 tick 后分发。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/2, /*token_limit*/0);

    SpanEvent span1 = MakeSpan(22, 201, 1000);
    SpanEvent span2 = MakeSpan(22, 202, 1100);

    ASSERT_EQ(manager.Push(span1), TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.Push(span2), TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.size(), 1u);
    auto iter = manager.index_by_trace_.find(22);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state, TraceSession::LifecycleState::Sealed);
    EXPECT_EQ(manager.sessions_[iter->second]->seal_reason, TraceSession::SealReason::Capacity);
    EXPECT_EQ(manager.sessions_[iter->second]->sealed_deadline_tick, 1u);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));

    SweepOneTick(manager, /*now_ms*/1000);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_EQ(repo.last_summary.trace_id, "22");
    EXPECT_EQ(repo.last_spans.size(), 2u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, PushSealsWhenTokenLimitReachedAndDispatchesAfterOneTick)
{
    // 目的：验证 token_limit 达阈值后不会立刻 dispatch，而是先 sealed 1 tick。
    // 说明：这里直接复用真实 TokenEstimator 估算值，验证“累计 token 达阈值 -> 自动分发”。
    // 这样可以保证测试跟线上口径一致，避免依赖手工篡改内部状态。
    ThreadPool pool(1);
    FakeTraceRepository repo;

    SpanEvent span1 = MakeSpan(77, 701, 1000);
    span1.name = "s1";
    span1.service_name = "svc";
    SpanEvent span2 = MakeSpan(77, 702, 1100);
    span2.name = "s2";
    span2.service_name = "svc";
    span2.attributes["env"] = "prod";

    TokenEstimator estimator;
    const size_t first_tokens = estimator.Estimate(span1);
    const size_t second_tokens = estimator.Estimate(span2);
    ASSERT_GT(first_tokens, 0u);
    ASSERT_GT(second_tokens, 0u);

    // 阈值设为“两条 span 估算之和”，确保第一条不触发、第二条触发。
    const size_t token_limit = first_tokens + second_tokens;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, token_limit);

    ASSERT_EQ(manager.Push(span1), TraceSessionManager::PushResult::Accepted);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));
    EXPECT_EQ(manager.size(), 1u);

    ASSERT_EQ(manager.Push(span2), TraceSessionManager::PushResult::Accepted);
    auto iter = manager.index_by_trace_.find(77);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state, TraceSession::LifecycleState::Sealed);
    EXPECT_EQ(manager.sessions_[iter->second]->seal_reason, TraceSession::SealReason::TokenLimit);
    EXPECT_EQ(manager.sessions_[iter->second]->sealed_deadline_tick, 1u);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));

    SweepOneTick(manager, /*now_ms*/1000);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_EQ(repo.last_summary.trace_id, "77");
    EXPECT_EQ(repo.last_spans.size(), 2u);
    EXPECT_GE(repo.last_summary.token_count, token_limit);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, PushDispatchesWhenDuplicateSpanIdAppears)
{
    // 目的：验证重复 span_id 会触发提前分发，避免脏数据持续污染当前会话。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(33, 301, 1000);

    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);

    // 重复 span_id 触发提前分发，是为了尽快“截断脏数据会话”，这里验证该保护逻辑不会被后续改动破坏。
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_EQ(repo.last_summary.trace_id, "33");
    EXPECT_EQ(repo.last_spans.size(), 1u);
    EXPECT_EQ(repo.last_spans.front().span_id, "301");

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, DispatchBuildsCorrectTraceTreeAndOrder)
{
    // 目的：验证 root-child 关系构建后，子节点会按 start_time_ms 排序进入落库顺序。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent root = MakeSpan(88, 801, 1000);
    SpanEvent child_late = MakeSpan(88, 803, 3000);
    child_late.parent_span_id = 801;
    SpanEvent child_early = MakeSpan(88, 802, 2000);
    child_early.parent_span_id = 801;
    child_early.trace_end = true;

    ASSERT_EQ(manager.Push(root), TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.Push(child_late), TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.Push(child_early), TraceSessionManager::PushResult::Accepted);

    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    ASSERT_EQ(repo.last_spans.size(), 3u);
    EXPECT_EQ(repo.last_spans[0].span_id, "801");
    EXPECT_EQ(repo.last_spans[1].span_id, "802");
    EXPECT_EQ(repo.last_spans[2].span_id, "803");

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, DispatchHandlesMissingParentAsRoot)
{
    // 目的：验证 parent_span_id 指向不存在节点时，该 span 不会丢失，仍能参与分发并落库。
    // 说明：当前实现会把该节点当作 root 参与遍历，但保留原始 parent_id 以便排障定位上游数据问题。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent orphan = MakeSpan(99, 901, 1000);
    orphan.parent_span_id = 9999;
    orphan.trace_end = true;

    ASSERT_EQ(manager.Push(orphan), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    ASSERT_EQ(repo.last_spans.size(), 1u);
    EXPECT_EQ(repo.last_spans[0].span_id, "901");
    ASSERT_TRUE(repo.last_spans[0].parent_id.has_value());
    EXPECT_EQ(repo.last_spans[0].parent_id.value(), "9999");

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, DispatchMarksCycleAsAnomalyWithoutInfiniteLoop)
{
    // 目的：直接验证序列化层的防环逻辑，确保出现环时不会无限递归并会写入 anomalies。
    TraceSessionManager manager(nullptr, nullptr, nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent span_a = MakeSpan(100, 1001, 1000);
    SpanEvent span_b = MakeSpan(100, 1002, 1100);

    TraceSessionManager::TraceIndex index;
    index.span_map[1001] = &span_a;
    index.span_map[1002] = &span_b;
    index.children[1001] = {1002};
    index.children[1002] = {1001};
    index.roots = {1001};

    std::vector<const SpanEvent*> order;
    const std::string payload = manager.SerializeTrace(index, &order);
    const nlohmann::json parsed = nlohmann::json::parse(payload);

    EXPECT_TRUE(parsed.contains("anomalies"));
    ASSERT_TRUE(parsed["anomalies"].is_array());
    EXPECT_FALSE(parsed["anomalies"].empty());
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0]->span_id, 1001u);
    EXPECT_EQ(order[1]->span_id, 1002u);
}

TEST_F(TraceSessionManagerUnitTest, DispatchPersistsSummaryAndSpansToRepository)
{
    // 目的：验证 manager 组装出的 summary/spans 是否按契约映射到 repository。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent root = MakeSpan(44, 401, 1000);
    root.name = "root-op";
    root.service_name = "order-service";
    root.end_time = 1300;
    root.status = SpanEvent::Status::Ok;

    SpanEvent child = MakeSpan(44, 402, 1100);
    child.parent_span_id = 401;
    child.name = "child-op";
    child.service_name = "order-service";
    child.end_time = 1200;
    child.status = SpanEvent::Status::Error;
    child.trace_end = true;

    ASSERT_EQ(manager.Push(root), TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.Push(child), TraceSessionManager::PushResult::Accepted);

    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    // 这里覆盖 summary/spans 字段映射，是为了防止未来字段重构时“编译通过但语义错位”。
    EXPECT_EQ(repo.last_summary.trace_id, "44");
    EXPECT_EQ(repo.last_summary.service_name, "order-service");
    EXPECT_EQ(repo.last_summary.span_count, 2u);
    EXPECT_EQ(repo.last_summary.start_time_ms, 1000);
    EXPECT_EQ(repo.last_summary.duration_ms, 300);

    ASSERT_EQ(repo.last_spans.size(), 2u);
    EXPECT_EQ(repo.last_spans[0].span_id, "401");
    EXPECT_FALSE(repo.last_spans[0].parent_id.has_value());
    EXPECT_EQ(repo.last_spans[0].status, "OK");

    EXPECT_EQ(repo.last_spans[1].span_id, "402");
    ASSERT_TRUE(repo.last_spans[1].parent_id.has_value());
    EXPECT_EQ(repo.last_spans[1].parent_id.value(), "401");
    EXPECT_EQ(repo.last_spans[1].status, "ERROR");

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, DispatchSkipsAnalysisWhenAiProviderIsNull)
{
    // 目的：验证 AI 依赖为空时仍可落库基础 trace 数据，确保最小部署模式可用。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(55, 501, 1000);
    span.trace_end = true;

    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);

    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    // trace_ai 为空是允许的最小部署模式，这里断言 analysis 指针为空，避免把可选依赖误变成强依赖。
    EXPECT_FALSE(repo.last_analysis.has_value());
    EXPECT_EQ(repo.last_summary.trace_id, "55");
    EXPECT_EQ(repo.last_spans.size(), 1u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, DispatchSendsNotificationOnlyForCriticalRisk)
{
    // 目的：验证告警策略只对 critical 发送通知，避免 warning/info 造成噪音告警。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    StubTraceAi ai;
    SpyNotifier notifier;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/10, /*token_limit*/0, &notifier);

    SpanEvent critical_span = MakeSpan(66, 601, 1000);
    critical_span.trace_end = true;
    ai.result.risk_level = RiskLevel::CRITICAL;
    ASSERT_EQ(manager.Push(critical_span), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntil([&notifier]() {
        return notifier.notify_trace_alert_called.load(std::memory_order_acquire);
    }));
    EXPECT_EQ(notifier.last_event.trace_id, "66");
    EXPECT_EQ(notifier.last_event.risk_level, "critical");

    // 第二段改为 non-critical，验证策略过滤是否生效。这里重置标记是为了避免第一次调用结果污染第二次断言。
    notifier.notify_trace_alert_called.store(false, std::memory_order_release);
    ai.result.risk_level = RiskLevel::WARNING;

    SpanEvent warning_span = MakeSpan(67, 602, 1100);
    warning_span.trace_end = true;
    ASSERT_EQ(manager.Push(warning_span), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.last_summary.trace_id == "67"; }));
    EXPECT_FALSE(notifier.notify_trace_alert_called.load(std::memory_order_acquire));

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, DispatchNotifiesAfterAnalysisEnqueueEvenIfBackgroundPrimaryFlushFails)
{
    // 目的：锁定当前双缓冲语义：通知是在 analysis 成功 enqueue 后触发，
    // 而不是等后台 primary flush 真正完成。这里主数据 flush 故意失败，验证 notifier 仍会触发。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    repo.save_atomic_return = false;
    StubTraceAi ai;
    ai.result.risk_level = RiskLevel::CRITICAL;
    SpyNotifier notifier;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), &ai, /*capacity*/10, /*token_limit*/0, &notifier);

    SpanEvent span = MakeSpan(111, 1101, 1000);
    span.trace_end = true;
    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);

    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    ASSERT_TRUE(WaitUntil([&notifier]() {
        return notifier.notify_trace_alert_called.load(std::memory_order_acquire);
    }));
    EXPECT_EQ(notifier.last_event.trace_id, "111");

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, PushRejectsWhenThreadPoolIsNull)
{
    // 目的：验证 thread_pool 为空时直接拒绝请求，而不是误报 accepted 后把 trace 静默丢掉。
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(nullptr, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(122, 1201, 1000);
    span.trace_end = true;

    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::RejectedUnavailable);
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));
}

TEST_F(TraceSessionManagerUnitTest, SealedTraceRollsBackToReadyRetryLaterWhenSubmitFails)
{
    // 目的：验证 sealed trace 到点 dispatch 时如果 submit 失败，不会蒸发，而是回滚到 ReadyRetryLater。
    ThreadPool pool(1, /*max_queue_size*/0);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(123, 12301, 1000);
    span.trace_end = true;

    EXPECT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.size(), 1u);
    auto iter = manager.index_by_trace_.find(123);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_LT(iter->second, manager.sessions_.size());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state,
              TraceSession::LifecycleState::Sealed);
    EXPECT_EQ(manager.sessions_[iter->second]->sealed_deadline_tick, 2u);

    SweepOneTick(manager, /*now_ms*/1000);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state,
              TraceSession::LifecycleState::Sealed);

    SweepOneTick(manager, /*now_ms*/1500);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 123, [](const TraceSession& session) {
        return session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater &&
               session.retry_count == 1u &&
               session.next_retry_tick == 3u;
    }));
    EXPECT_EQ(manager.active_sessions_, 1u);
    EXPECT_EQ(manager.total_buffered_spans_, 1u);
    iter = manager.index_by_trace_.find(123);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_LT(iter->second, manager.sessions_.size());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state,
              TraceSession::LifecycleState::ReadyRetryLater);
    EXPECT_EQ(manager.sessions_[iter->second]->retry_count, 1u);
    EXPECT_EQ(manager.sessions_[iter->second]->next_retry_tick, 3u);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));

    const TraceSessionManager::RuntimeStatsSnapshot stats = manager.SnapshotRuntimeStats();
    EXPECT_EQ(stats.dispatch_count, 1u);
    EXPECT_EQ(stats.submit_ok_count, 0u);
    EXPECT_EQ(stats.submit_fail_count, 1u);
    EXPECT_EQ(stats.worker_begin_count, 0u);
    EXPECT_EQ(stats.worker_done_count, 0u);
    EXPECT_EQ(stats.ai_calls, 0u);
    EXPECT_EQ(stats.analysis_enqueue_calls, 0u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, RetryDelayBackoffGrowsOnRepeatedSubmitFailure)
{
    // 目的：验证 ready retry 会话连续失败时会增加 retry_count，并把 next_retry_tick 往后推，而不是每 tick 原地打桩。
    ThreadPool pool(1, /*max_queue_size*/0);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(124, 12401, 1000);
    span.trace_end = true;
    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);

    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 124, [](const TraceSession& session) {
        return session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater &&
               session.retry_count == 1u &&
               session.next_retry_tick == 3u;
    }));

    auto iter = manager.index_by_trace_.find(124);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->retry_count, 1u);
    EXPECT_EQ(manager.sessions_[iter->second]->next_retry_tick, 3u);

    manager.SweepExpiredSessions(/*now_ms*/2000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/8);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 124, [](const TraceSession& session) {
        return session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater &&
               session.retry_count == 2u &&
               session.next_retry_tick == 5u;
    }));

    iter = manager.index_by_trace_.find(124);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state,
              TraceSession::LifecycleState::ReadyRetryLater);
    EXPECT_EQ(manager.sessions_[iter->second]->retry_count, 2u);
    EXPECT_EQ(manager.sessions_[iter->second]->next_retry_tick, 5u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, RetryBackoffDoesNotRetryBeforeNextRetryTick)
{
    // 目的：验证 ready retry 会话在未到 next_retry_tick 前不会提前重试；
    // 必须等 current_tick 追到 next_retry_tick，retry_count 才会再次增加。
    ThreadPool pool(1, /*max_queue_size*/0);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(125, 12501, 1000);
    span.trace_end = true;
    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);

    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 125, [](const TraceSession& session) {
        return session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater &&
               session.retry_count == 1u &&
               session.next_retry_tick == 3u;
    }));

    auto iter = manager.index_by_trace_.find(125);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->retry_count, 1u);
    EXPECT_EQ(manager.sessions_[iter->second]->next_retry_tick, 3u);

    // tick=3：允许第一次 retry，因此会再次失败并把 next_retry_tick 推到 5。
    manager.SweepExpiredSessions(/*now_ms*/2000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/8);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 125, [](const TraceSession& session) {
        return session.retry_count == 2u && session.next_retry_tick == 5u;
    }));
    iter = manager.index_by_trace_.find(125);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->retry_count, 2u);
    EXPECT_EQ(manager.sessions_[iter->second]->next_retry_tick, 5u);

    // tick=4：还没到 next_retry_tick=5，不应该提前再次重试。
    manager.SweepExpiredSessions(/*now_ms*/2500, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/8);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 125, [](const TraceSession& session) {
        return session.retry_count == 2u && session.next_retry_tick == 5u;
    }));
    iter = manager.index_by_trace_.find(125);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->retry_count, 2u);
    EXPECT_EQ(manager.sessions_[iter->second]->next_retry_tick, 5u);

    // tick=5：此时才允许第三次尝试，因此 retry_count 应增加到 3。
    manager.SweepExpiredSessions(/*now_ms*/3000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/8);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 125, [](const TraceSession& session) {
        return session.retry_count == 3u && session.next_retry_tick == 9u;
    }));
    iter = manager.index_by_trace_.find(125);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->retry_count, 3u);
    EXPECT_EQ(manager.sessions_[iter->second]->next_retry_tick, 9u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, RetrySuccessDoesNotAppendPrimaryTwiceAfterSubmitFailure)
{
    // 目的：验证 primary 在首次 submit 失败后不会重复 append；
    // 既然回滚的是 session 生命周期，不是回滚已经入缓冲的主数据，那下一次重试只能补 analysis，不能再写第二份 summary/spans。
    ThreadPool rejected_pool(0, /*max_queue_size*/0);
    ThreadPool success_pool(1);
    FakeTraceRepository repo;
    StubTraceAi ai;
    BufferedTraceRepository::Config buffer_config;
    buffer_config.primary_flush_interval_ms = 20;
    buffer_config.analysis_flush_interval_ms = 20;
    auto sink = std::shared_ptr<TraceRepository>(&repo, [](TraceRepository*) {});
    auto buffered_repo = std::make_unique<BufferedTraceRepository>(std::move(sink), buffer_config);
    TraceSessionManager manager(&rejected_pool, buffered_repo.get(), &ai, /*capacity*/10, /*token_limit*/0);

    SpanEvent span = MakeSpan(126, 12601, 1000);
    span.trace_end = true;

    EXPECT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);

    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 126, [](const TraceSession& session) {
        return session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater &&
               session.primary_enqueued;
    }));

    auto iter = manager.index_by_trace_.find(126);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_TRUE(manager.sessions_[iter->second]->primary_enqueued);

    ASSERT_TRUE(WaitUntil([&repo]() {
        return repo.save_summary_count.load(std::memory_order_acquire) >= 1 &&
               repo.save_spans_count.load(std::memory_order_acquire) >= 1;
    }, 1000));
    EXPECT_EQ(repo.save_summary_count.load(std::memory_order_acquire), 1);
    EXPECT_EQ(repo.save_spans_count.load(std::memory_order_acquire), 1);
    EXPECT_EQ(repo.save_analysis_count.load(std::memory_order_acquire), 0);

    // 第一次失败后把 thread_pool 切回可用实现，再手动重投同一 trace。
    manager.thread_pool_ = &success_pool;
    ASSERT_TRUE(manager.Dispatch(126));

    ASSERT_TRUE(WaitUntil([&repo]() {
        return repo.save_analysis_count.load(std::memory_order_acquire) >= 1;
    }, 1000));
    EXPECT_EQ(repo.save_summary_count.load(std::memory_order_acquire), 1);
    EXPECT_EQ(repo.save_spans_count.load(std::memory_order_acquire), 1);
    EXPECT_EQ(repo.save_analysis_count.load(std::memory_order_acquire), 1);

    const TraceSessionManager::RuntimeStatsSnapshot stats = manager.SnapshotRuntimeStats();
    EXPECT_EQ(stats.submit_fail_count, 1u);
    EXPECT_EQ(stats.submit_ok_count, 1u);
    EXPECT_EQ(stats.worker_done_count, 1u);

    success_pool.shutdown();
    rejected_pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, ReadyRetryLaterDoesNotMergeLateSpan)
{
    // 目的：验证 submit 失败后的 ReadyRetryLater 会话不再吸收新 span，避免 primary 已收口后又在内存里长出新版本。
    ThreadPool pool(1, /*max_queue_size*/0);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent first = MakeSpan(127, 12701, 1000);
    first.trace_end = true;
    ASSERT_EQ(manager.Push(first), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 127, [](const TraceSession& session) {
        return session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater;
    }));

    auto iter = manager.index_by_trace_.find(127);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state,
              TraceSession::LifecycleState::ReadyRetryLater);
    EXPECT_EQ(manager.sessions_[iter->second]->spans.size(), 1u);

    SpanEvent late = MakeSpan(127, 12702, 1100);
    EXPECT_EQ(manager.Push(late), TraceSessionManager::PushResult::AcceptedDeferred);

    iter = manager.index_by_trace_.find(127);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    ASSERT_TRUE(manager.sessions_[iter->second] != nullptr);
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state,
              TraceSession::LifecycleState::ReadyRetryLater);
    EXPECT_EQ(manager.sessions_[iter->second]->spans.size(), 1u);
    EXPECT_EQ(manager.sessions_[iter->second]->span_ids.count(12702), 0u);
    EXPECT_EQ(manager.total_buffered_spans_, 1u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, CompletedTombstoneAbsorbsLateSpanWithoutCreatingNewSession)
{
    // 目的：验证 trace 成功进入 worker 队列后会进入短暂 TIME_WAIT；
    // 后续同 trace_key 的晚到 span 应直接被吞掉，而不是再复活成新的聚合会话。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent first = MakeSpan(128, 12801, 1000);
    first.trace_end = true;
    ASSERT_EQ(manager.Push(first), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);

    ASSERT_TRUE(WaitUntil([&repo]() {
        return repo.save_summary_count.load(std::memory_order_acquire) >= 1 &&
               repo.save_spans_count.load(std::memory_order_acquire) >= 1;
    }));
    EXPECT_EQ(manager.size(), 0u);
    ASSERT_EQ(manager.completed_trace_expire_tick_.count(128), 1u);
    EXPECT_GT(manager.completed_trace_expire_tick_[128], manager.current_tick_);

    const int summary_count_before = repo.save_summary_count.load(std::memory_order_acquire);
    const int span_batch_count_before = repo.save_spans_count.load(std::memory_order_acquire);

    SpanEvent late = MakeSpan(128, 12802, 1100);
    EXPECT_EQ(manager.Push(late), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_EQ(manager.completed_trace_expire_tick_.count(128), 1u);
    EXPECT_EQ(repo.save_summary_count.load(std::memory_order_acquire), summary_count_before);
    EXPECT_EQ(repo.save_spans_count.load(std::memory_order_acquire), span_batch_count_before);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, CompletedTombstoneExpiresAndAllowsTraceKeyReuse)
{
    // 目的：验证 TIME_WAIT tombstone 只负责短时间延迟遗忘；
    // 一旦到期，同 trace_key 应允许重新创建新会话。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/10, /*token_limit*/0);

    SpanEvent first = MakeSpan(129, 12901, 1000);
    first.trace_end = true;
    ASSERT_EQ(manager.Push(first), TraceSessionManager::PushResult::Accepted);
    SweepTraceEndSealWindow(manager);

    ASSERT_TRUE(WaitUntil([&repo]() {
        return repo.save_summary_count.load(std::memory_order_acquire) >= 1 &&
               repo.save_spans_count.load(std::memory_order_acquire) >= 1;
    }));
    ASSERT_EQ(manager.completed_trace_expire_tick_.count(129), 1u);

    // 当前 tick 约为 2；tombstone 默认保留 25 tick。
    // 这里一次性推进 30 tick，验证即使没有活跃 session，completed wheel 也会被正常回收。
    manager.SweepExpiredSessions(/*now_ms*/16500, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/8);
    EXPECT_EQ(manager.completed_trace_expire_tick_.count(129), 0u);

    SpanEvent reused = MakeSpan(129, 12902, 1200);
    EXPECT_EQ(manager.Push(reused), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.size(), 1u);
    EXPECT_EQ(manager.index_by_trace_.count(129), 1u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, RebuildTimeWheelKeepsReadyRetryLaterOnRetrySchedule)
{
    // 目的：验证 ReadyRetryLater 会话在重建时间轮后仍走“快速重投”语义，而不是退回普通 idle timeout。
    ThreadPool pool(1, /*max_queue_size*/0);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(
        &pool,
        buffered_repo.get(),
        nullptr,
        /*capacity*/10,
        /*token_limit*/0,
        /*notifier*/nullptr,
        /*idle_timeout_ms*/5000,
        /*wheel_tick_ms*/500);

    SpanEvent span = MakeSpan(124, 12401, 1000);
    span.trace_end = true;
    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);

    SweepTraceEndSealWindow(manager);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 124, [](const TraceSession& session) {
        return session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater;
    }));

    auto idx_iter = manager.index_by_trace_.find(124);
    ASSERT_NE(idx_iter, manager.index_by_trace_.end());
    ASSERT_LT(idx_iter->second, manager.sessions_.size());
    TraceSession& session = *manager.sessions_[idx_iter->second];
    ASSERT_EQ(session.lifecycle_state, TraceSession::LifecycleState::ReadyRetryLater);

    manager.RebuildTimeWheel();

    const uint64_t expected_retry_tick = session.next_retry_tick;
    const size_t expected_slot = static_cast<size_t>(expected_retry_tick % manager.wheel_size_);
    bool found_retry_node = false;
    for (const auto& node : manager.time_wheel_[expected_slot]) {
        if (node.trace_key == session.trace_key &&
            node.version == session.timer_version &&
            node.epoch == session.session_epoch &&
            node.expire_tick == expected_retry_tick) {
            found_retry_node = true;
            break;
        }
    }
    EXPECT_TRUE(found_retry_node);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, SweepTimeout_DispatchesSessionWithoutTraceEnd)
{
    // 目的：验证“无 trace_end + 到达 idle 超时”会触发分发。
    // 这条用例专门覆盖时间轮最基础行为：不到期不分发，到期后分发。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(
        &pool,
        buffered_repo.get(),
        nullptr,
        /*capacity*/10,
        /*token_limit*/0,
        /*notifier*/nullptr,
        /*idle_timeout_ms*/5000,
        /*wheel_tick_ms*/500);

    SpanEvent span = MakeSpan(133, 1301, 1000);
    ASSERT_EQ(manager.Push(span), TraceSessionManager::PushResult::Accepted);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));
    EXPECT_EQ(manager.size(), 1u);

    // 第一次 sweep：仅推进到早期时间点，不应该提前触发分发。
    manager.SweepExpiredSessions(/*now_ms*/1000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));
    EXPECT_EQ(manager.size(), 1u);

    // 第二次 sweep：推进到超过 idle_timeout 的时间点，应该触发分发。
    manager.SweepExpiredSessions(/*now_ms*/6000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);

    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    EXPECT_EQ(manager.size(), 0u);
    EXPECT_EQ(repo.last_summary.trace_id, "133");
    EXPECT_EQ(repo.last_spans.size(), 1u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, SweepTimeout_ReschedulePreventsEarlyDispatch)
{
    // 目的：验证同一 trace 续命后，旧超时计划不会提前触发分发。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(
        &pool,
        buffered_repo.get(),
        nullptr,
        /*capacity*/10,
        /*token_limit*/0,
        /*notifier*/nullptr,
        /*idle_timeout_ms*/5000,
        /*wheel_tick_ms*/500);

    SpanEvent span1 = MakeSpan(201, 2001, 1000);
    ASSERT_EQ(manager.Push(span1), TraceSessionManager::PushResult::Accepted);

    manager.SweepExpiredSessions(/*now_ms*/1000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));

    // 续命：同 trace 新 span 进入后会重排超时计划。
    SpanEvent span2 = MakeSpan(201, 2002, 1100);
    ASSERT_EQ(manager.Push(span2), TraceSessionManager::PushResult::Accepted);

    // 推到旧计划到期点：不应该触发（旧节点应被 version 校验淘汰）。
    manager.SweepExpiredSessions(/*now_ms*/5500, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));
    EXPECT_EQ(manager.size(), 1u);

    // 推到新计划到期点：此时才应该触发分发。
    manager.SweepExpiredSessions(/*now_ms*/6000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 1);
    EXPECT_EQ(repo.last_summary.trace_id, "201");

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, SweepTimeout_TraceKeyReuseDoesNotTriggerNewSessionByOldNode)
{
    // 目的：验证 trace_key 复用时，旧会话的轮子节点不会误命中新会话（epoch 防串台）。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(
        &pool,
        buffered_repo.get(),
        nullptr,
        /*capacity*/10,
        /*token_limit*/0,
        /*notifier*/nullptr,
        /*idle_timeout_ms*/5000,
        /*wheel_tick_ms*/500);

    SpanEvent old_span = MakeSpan(301, 3001, 1000);
    ASSERT_EQ(manager.Push(old_span), TraceSessionManager::PushResult::Accepted);

    // 先推进一个 tick，让后续新会话和旧会话落在不同到期 tick。
    manager.SweepExpiredSessions(/*now_ms*/1000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));

    // 模拟“老会话已被其他路径分发并移除”，但旧轮子节点还残留在槽里。
    manager.sessions_.clear();
    manager.index_by_trace_.clear();

    SpanEvent new_span = MakeSpan(301, 3002, 1200);
    ASSERT_EQ(manager.Push(new_span), TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.size(), 1u);

    // 扫到老节点的到期 tick：不应误分发新会话。
    manager.SweepExpiredSessions(/*now_ms*/5500, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    EXPECT_FALSE(repo.save_atomic_called.load(std::memory_order_acquire));
    EXPECT_EQ(manager.size(), 1u);

    // 扫到新会话的到期 tick：才应该触发分发。
    manager.SweepExpiredSessions(/*now_ms*/6000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_called.load(std::memory_order_acquire); }));
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 1);
    EXPECT_EQ(repo.last_summary.trace_id, "301");

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, SweepTimeout_MaxDispatchPerTickDefersRemainingSessions)
{
    // 目的：验证单轮分发上限生效，未处理节点会顺延而不是丢失。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(
        &pool,
        buffered_repo.get(),
        nullptr,
        /*capacity*/10,
        /*token_limit*/0,
        /*notifier*/nullptr,
        /*idle_timeout_ms*/5000,
        /*wheel_tick_ms*/500);

    ASSERT_EQ(manager.Push(MakeSpan(401, 4001, 1000)), TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.Push(MakeSpan(402, 4002, 1000)), TraceSessionManager::PushResult::Accepted);

    manager.SweepExpiredSessions(/*now_ms*/1000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 0);

    // 同一轮到期两条 trace，但上限是 1，只应分发一条。
    manager.SweepExpiredSessions(/*now_ms*/6000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_count.load(std::memory_order_acquire) >= 1; }));
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 1);

    // 下一轮应把被顺延的那条补分发。
    manager.SweepExpiredSessions(/*now_ms*/6500, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_count.load(std::memory_order_acquire) >= 2; }));
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 2);

    std::set<std::string> trace_ids;
    {
        std::lock_guard<std::mutex> lock(repo.saved_trace_ids_mutex);
        trace_ids.insert(repo.saved_trace_ids.begin(), repo.saved_trace_ids.end());
    }
    EXPECT_EQ(trace_ids.count("401"), 1u);
    EXPECT_EQ(trace_ids.count("402"), 1u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, SweepTimeout_DeduplicatesSameTraceInSingleSweep)
{
    // 目的：验证同一槽里出现同 trace 的重复有效节点时，只会分发一次。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(
        &pool,
        buffered_repo.get(),
        nullptr,
        /*capacity*/10,
        /*token_limit*/0,
        /*notifier*/nullptr,
        /*idle_timeout_ms*/5000,
        /*wheel_tick_ms*/500);

    ASSERT_EQ(manager.Push(MakeSpan(501, 5001, 1000)), TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.size(), 1u);
    auto idx_iter = manager.index_by_trace_.find(501);
    ASSERT_NE(idx_iter, manager.index_by_trace_.end());
    TraceSession& session = *manager.sessions_[idx_iter->second];
    const uint64_t expire_tick = manager.current_tick_ + manager.timeout_ticks_;
    const size_t slot = static_cast<size_t>(expire_tick % manager.wheel_size_);

    // 人工塞一个“同 trace + 同 version + 同 epoch”的重复节点，模拟异常重复入槽。
    TraceSessionManager::TimeWheelNode duplicate;
    duplicate.trace_key = session.trace_key;
    duplicate.version = session.timer_version;
    duplicate.epoch = session.session_epoch;
    duplicate.expire_tick = expire_tick;
    manager.time_wheel_[slot].push_back(duplicate);

    manager.SweepExpiredSessions(/*now_ms*/1000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/8);
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 0);

    manager.SweepExpiredSessions(/*now_ms*/6000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/8);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_count.load(std::memory_order_acquire) >= 1; }));
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 1);
    EXPECT_EQ(repo.last_summary.trace_id, "501");

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, SweepTimeout_RebuildOnIdleTimeoutChangeUsesNewSchedule)
{
    // 目的：验证动态修改 idle_timeout 后，重建时间轮并按新超时策略触发分发。
    ThreadPool pool(1);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(
        &pool,
        buffered_repo.get(),
        nullptr,
        /*capacity*/10,
        /*token_limit*/0,
        /*notifier*/nullptr,
        /*idle_timeout_ms*/5000,
        /*wheel_tick_ms*/500);

    ASSERT_EQ(manager.Push(MakeSpan(601, 6001, 1000)), TraceSessionManager::PushResult::Accepted);

    manager.SweepExpiredSessions(/*now_ms*/1000, /*idle_timeout_ms*/5000, /*max_dispatch_per_tick*/1);
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 0);

    // 将 idle_timeout 下调为 1000ms，会触发重建并采用新的 timeout_ticks。
    manager.SweepExpiredSessions(/*now_ms*/1500, /*idle_timeout_ms*/1000, /*max_dispatch_per_tick*/1);
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 0);
    EXPECT_EQ(manager.size(), 1u);

    // 再推进一个 tick，按新超时应到期分发。
    manager.SweepExpiredSessions(/*now_ms*/2000, /*idle_timeout_ms*/1000, /*max_dispatch_per_tick*/1);
    ASSERT_TRUE(WaitUntil([&repo]() { return repo.save_atomic_count.load(std::memory_order_acquire) >= 1; }));
    EXPECT_EQ(repo.save_atomic_count.load(std::memory_order_acquire), 1);
    EXPECT_EQ(repo.last_summary.trace_id, "601");

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, PushRejectsNewTraceButAllowsExistingTraceWhenOverload)
{
    // 目的：验证进入 overload 后只拒绝新 trace，已在内存中的老 trace 仍尽量放行，避免聚合被截断。
    ThreadPool pool(1, /*max_queue_size*/100);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool,
                                buffered_repo.get(),
                                nullptr,
                                /*capacity*/100,
                                /*token_limit*/0,
                                nullptr,
                                /*idle_timeout_ms*/5000,
                                /*wheel_tick_ms*/500,
                                /*wheel_size*/512,
                                /*buffered_span_hard_limit*/10,
                                /*active_session_hard_limit*/10);

    for (size_t i = 0; i < 7; ++i) {
        ASSERT_EQ(manager.Push(MakeSpan(701, 7001 + i, 1000 + static_cast<int64_t>(i))),
                  TraceSessionManager::PushResult::Accepted);
    }

    EXPECT_EQ(manager.overload_state_, TraceSessionManager::OverloadState::Overload);
    EXPECT_EQ(manager.total_buffered_spans_, 7u);
    EXPECT_EQ(manager.active_sessions_, 1u);

    EXPECT_EQ(manager.Push(MakeSpan(702, 7101, 2000)),
              TraceSessionManager::PushResult::RejectedOverload);
    EXPECT_EQ(manager.total_buffered_spans_, 7u);
    EXPECT_EQ(manager.active_sessions_, 1u);

    EXPECT_EQ(manager.Push(MakeSpan(701, 7008, 2001)),
              TraceSessionManager::PushResult::Accepted);
    EXPECT_EQ(manager.total_buffered_spans_, 8u);
    EXPECT_EQ(manager.active_sessions_, 1u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, PushRejectsExistingTraceWhenCritical)
{
    // 目的：验证进入 critical 后连老 trace 也允许拒绝，优先保住进程，不继续用完整性换 OOM 风险。
    ThreadPool pool(1, /*max_queue_size*/100);
    FakeTraceRepository repo;
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool,
                                buffered_repo.get(),
                                nullptr,
                                /*capacity*/100,
                                /*token_limit*/0,
                                nullptr,
                                /*idle_timeout_ms*/5000,
                                /*wheel_tick_ms*/500,
                                /*wheel_size*/512,
                                /*buffered_span_hard_limit*/10,
                                /*active_session_hard_limit*/10);

    for (size_t i = 0; i < 9; ++i) {
        ASSERT_EQ(manager.Push(MakeSpan(801, 8001 + i, 1000 + static_cast<int64_t>(i))),
                  TraceSessionManager::PushResult::Accepted);
    }

    EXPECT_EQ(manager.overload_state_, TraceSessionManager::OverloadState::Critical);
    EXPECT_EQ(manager.total_buffered_spans_, 9u);
    EXPECT_EQ(manager.active_sessions_, 1u);

    EXPECT_EQ(manager.Push(MakeSpan(801, 8010, 2000)),
              TraceSessionManager::PushResult::RejectedOverload);
    EXPECT_EQ(manager.Push(MakeSpan(802, 8201, 2001)),
              TraceSessionManager::PushResult::RejectedOverload);
    EXPECT_EQ(manager.total_buffered_spans_, 9u);
    EXPECT_EQ(manager.active_sessions_, 1u);

    pool.shutdown();
}

TEST_F(TraceSessionManagerUnitTest, PushReturnsAcceptedDeferredWhenSubmitFails)
{
    // 目的：保留最小协议断言：容量封口后的 trace 在 dispatch submit 失败时，
    // 会回滚成 ReadyRetryLater，而不是丢失。
    ThreadPool pool(1, 10);
    FakeTraceRepository repo;
    // 设 capacity=1。现在语义不是“立刻 dispatch”，而是先 sealed 1 tick，再尝试 dispatch。
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, /*capacity*/1, /*token_limit*/0);

    // 模拟线程池已关闭，等 sweep 到 sealed deadline 时 submit 必定返回 false。
    pool.shutdown();

    SpanEvent span = MakeSpan(901, 9001, 1000);
    TraceSessionManager::PushResult result = manager.Push(span);

    EXPECT_EQ(result, TraceSessionManager::PushResult::Accepted);
    ASSERT_EQ(manager.size(), 1u);
    auto iter = manager.index_by_trace_.find(901);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state,
              TraceSession::LifecycleState::Sealed);
    
    SweepOneTick(manager, /*now_ms*/1000);
    ASSERT_TRUE(WaitUntilSessionSatisfies(manager, 901, [](const TraceSession& session) {
        return session.lifecycle_state == TraceSession::LifecycleState::ReadyRetryLater &&
               session.retry_count == 1u &&
               session.next_retry_tick == 2u;
    }));
    iter = manager.index_by_trace_.find(901);
    ASSERT_NE(iter, manager.index_by_trace_.end());
    EXPECT_EQ(manager.sessions_[iter->second]->lifecycle_state,
              TraceSession::LifecycleState::ReadyRetryLater);
    EXPECT_EQ(manager.size(), 1u);
    EXPECT_EQ(manager.sessions_[iter->second]->retry_count, 1u);
    EXPECT_EQ(manager.sessions_[iter->second]->next_retry_tick, 2u);
}

TEST_F(TraceSessionManagerUnitTest, BackpressureRecoversWhenWatermarkDropsBelowLow)
{
    // 目的：验证背压状态机的“迟滞回线”逻辑，即进入 Overload 后需跌破 Low 水位（0.55）才恢复。
    ThreadPool pool(1, 100);
    FakeTraceRepository repo;
    // Hard limit = 100 -> Low = 55, High = 75, Critical = 90
    auto buffered_repo = MakeBufferedTraceRepository(&repo);
    TraceSessionManager manager(&pool, buffered_repo.get(), nullptr, 100, 0, nullptr, 5000, 500, 512, 100);

    // 1. 达到 High 水位 (76 > 75) 触发 Overload。
    for (size_t i = 0; i < 76; ++i) {
        manager.Push(MakeSpan(1001, 10000 + i, 1000));
    }
    EXPECT_EQ(manager.overload_state_, TraceSessionManager::OverloadState::Overload);

    // 2. 此时拒绝新 Trace。
    EXPECT_EQ(manager.Push(MakeSpan(1002, 20000, 1000)), 
              TraceSessionManager::PushResult::RejectedOverload);

    // 3. 释放部分数据，使水位跌破 Low (55)。
    // 这里我们直接 Dispatch 掉 Trace 1001，使 total_buffered_spans_ 归零。
    manager.Dispatch(1001);
    
    // 4. 验证状态机恢复，且新 Trace 现在能被接收。
    EXPECT_EQ(manager.overload_state_, TraceSessionManager::OverloadState::Normal);
    EXPECT_EQ(manager.Push(MakeSpan(1002, 20000, 1000)), 
              TraceSessionManager::PushResult::Accepted);
    
    pool.shutdown();
}
