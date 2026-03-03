#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "ai/TraceAiProvider.h"
#include "core/TraceSessionManager.h"
#include "notification/INotifier.h"
#include "persistence/TraceRepository.h"

namespace
{
// 用 FakeRepository 记录调用入参，目的是让单测只验证 TraceSessionManager 组装与调用是否正确，
// 不依赖真实 SQLite，从而把失败定位收敛在 manager 逻辑本身。
class FakeTraceRepository : public TraceRepository
{
public:
    bool save_summary_called = false;
    bool save_spans_called = false;
    bool save_analysis_called = false;
    bool save_prompt_called = false;
    bool save_atomic_called = false;

    bool save_atomic_return = true;

    TraceSummary last_summary;
    std::vector<TraceSpanRecord> last_spans;
    std::optional<TraceAnalysisRecord> last_analysis;
    std::optional<PromptDebugRecord> last_prompt_debug;

    bool SaveSingleTraceSummary(const TraceSummary& summary) override
    {
        save_summary_called = true;
        last_summary = summary;
        return true;
    }

    bool SaveSingleTraceSpans(const std::string&,
                              const std::vector<TraceSpanRecord>& spans) override
    {
        save_spans_called = true;
        last_spans = spans;
        return true;
    }

    bool SaveSingleTraceAnalysis(const TraceAnalysisRecord& analysis) override
    {
        save_analysis_called = true;
        last_analysis = analysis;
        return true;
    }

    bool SaveSinglePromptDebug(const PromptDebugRecord& record) override
    {
        save_prompt_called = true;
        last_prompt_debug = record;
        return true;
    }

    bool SaveSingleTraceAtomic(const TraceSummary& summary,
                               const std::vector<TraceSpanRecord>& spans,
                               const TraceAnalysisRecord* analysis,
                               const PromptDebugRecord* prompt_debug) override
    {
        save_atomic_called = true;
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
    bool called = false;
    std::string last_payload;

    LogAnalysisResult AnalyzeTrace(const std::string& trace_payload) override
    {
        called = true;
        last_payload = trace_payload;
        return result;
    }
};

// 用 SpyNotifier 只记录是否触发通知以及最后一次事件，目的是验证“何时通知”而不是测试 webhook 发送细节。
class SpyNotifier : public INotifier
{
public:
    bool notify_called = false;
    bool notify_trace_alert_called = false;
    std::string last_trace_id;
    nlohmann::json last_content;
    TraceAlertEvent last_event;

    void notify(const std::string& trace_id, const nlohmann::json& content) override
    {
        notify_called = true;
        last_trace_id = trace_id;
        last_content = content;
    }

    void notifyTraceAlert(const TraceAlertEvent& event) override
    {
        notify_trace_alert_called = true;
        last_event = event;
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
};

TEST_F(TraceSessionManagerUnitTest, PushDispatchesWhenTraceEndIsTrue)
{
    // 先保留空实现占位，目的是先锁定“显式结束触发分发”这个核心行为，避免后续补测时遗漏。
    GTEST_SKIP() << "TODO: 补充 trace_end=true 时的分发断言。";
}

TEST_F(TraceSessionManagerUnitTest, PushDispatchesWhenCapacityReached)
{
    // 该用例用于覆盖容量阈值触发路径，提前占位可以防止后续只测 happy path 导致回归漏检。
    GTEST_SKIP() << "TODO: 补充 capacity 达到上限后的分发断言。";
}

TEST_F(TraceSessionManagerUnitTest, PushDispatchesWhenTokenLimitReached)
{
    // token_limit 是软背压的关键入口，先占位能提醒我们后续必须验证阈值触发是否稳定。
    GTEST_SKIP() << "TODO: 补充 token_limit 触发分发的断言。";
}

TEST_F(TraceSessionManagerUnitTest, PushDispatchesWhenDuplicateSpanIdAppears)
{
    // 重复 span_id 是真实生产中高频脏数据场景，先占位避免后续遗漏异常路径测试。
    GTEST_SKIP() << "TODO: 补充 duplicate span_id 触发分发的断言。";
}

TEST_F(TraceSessionManagerUnitTest, DispatchBuildsCorrectTraceTreeAndOrder)
{
    // 聚合正确性是 TraceSessionManager 的核心价值，先占位明确后续要验证父子关系与遍历顺序。
    GTEST_SKIP() << "TODO: 补充 trace 树构建与 DFS 顺序断言。";
}

TEST_F(TraceSessionManagerUnitTest, DispatchHandlesMissingParentAsRoot)
{
    // 缺失父节点是上游乱序或数据缺失的常见情况，先占位保证后续验证容错策略。
    GTEST_SKIP() << "TODO: 补充缺失 parent_span_id 时的 root 降级断言。";
}

TEST_F(TraceSessionManagerUnitTest, DispatchMarksCycleAsAnomalyWithoutInfiniteLoop)
{
    // 环路数据会引发递归风险，先占位是为了后续明确验证“检测到环且不死循环”。
    GTEST_SKIP() << "TODO: 补充 cycle anomaly 的序列化断言。";
}

TEST_F(TraceSessionManagerUnitTest, DispatchPersistsSummaryAndSpansToRepository)
{
    // 该用例聚焦 manager 到 repository 的契约，先占位保证后续会验证落库参数映射完整性。
    GTEST_SKIP() << "TODO: 补充 SaveSingleTraceAtomic 的调用参数断言。";
}

TEST_F(TraceSessionManagerUnitTest, DispatchSkipsAnalysisWhenAiProviderIsNull)
{
    // 空 AI 依赖是最小部署形态，先占位防止后续实现把可选依赖错误变成必选依赖。
    GTEST_SKIP() << "TODO: 补充 trace_ai=nullptr 时 analysis 为空的断言。";
}

TEST_F(TraceSessionManagerUnitTest, DispatchSendsNotificationOnlyForCriticalRisk)
{
    // 告警策略要求只发 critical，先占位可避免未来策略改动时误发大量低风险通知。
    GTEST_SKIP() << "TODO: 补充 risk_level 过滤与 notifyTraceAlert 调用断言。";
}

TEST_F(TraceSessionManagerUnitTest, DispatchDoesNotNotifyWhenSaveFails)
{
    // 先落库后通知是为了保证可追溯性，先占位确保失败路径不会产生“通知成功但无数据”不一致。
    GTEST_SKIP() << "TODO: 补充 save 失败时不发通知的断言。";
}

TEST_F(TraceSessionManagerUnitTest, DispatchReturnsSafelyWhenThreadPoolIsNull)
{
    // 无线程池时应安全返回，先占位用于保障最小构造下不会崩溃。
    GTEST_SKIP() << "TODO: 补充 thread_pool=nullptr 时的健壮性断言。";
}
