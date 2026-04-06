#include <gtest/gtest.h>

#include "core/ServiceRuntimeAccumulator.h"

namespace
{
// 这里先用最小 observation 构造测试输入，目的是先把服务监控运行态的契约锁死，
// 后面再逐步把真正的 TraceSessionManager 观测结果接进来。
PrimaryObservation MakePrimaryObservation()
{
    PrimaryObservation observation;
    observation.trace_id = "trace-1";
    observation.trace_end_time_ms = 2000;
    observation.trace_risk_level = "warning";

    PrimaryServiceObservation service;
    service.service_name = "order-service";
    service.latest_exception_time_ms = 1900;
    service.error_trace_hit = true;
    service.error_span_count = 2;
    service.error_span_duration_sum_ms = 120;

    PrimaryOperationObservation operation;
    operation.operation_name = "create-order";
    operation.error_span_count = 2;
    operation.error_span_duration_sum_ms = 120;
    service.operations.push_back(operation);

    observation.services.push_back(service);
    return observation;
}

AnalysisObservation MakeAnalysisObservation()
{
    AnalysisObservation observation;
    observation.trace_id = "trace-1";
    observation.trace_end_time_ms = 2000;
    observation.trace_risk_level = "warning";
    observation.trace_summary = "订单创建链路在库存确认阶段超时。";

    AnalysisServiceSample sample;
    sample.service_name = "order-service";
    sample.representative_operation_name = "create-order";
    sample.sample_time_ms = 1900;
    sample.duration_ms = 120;
    sample.sample_risk_level = "warning";
    observation.service_samples.push_back(sample);
    return observation;
}
} // namespace

TEST(ServiceRuntimeAccumulatorTest, BuildSnapshotReturnsPublishedEmptySnapshotByDefault)
{
    ServiceRuntimeAccumulator accumulator(/*service_top_k*/4, /*operation_top_k*/6, /*recent_sample_limit*/3);

    // 刚构造时还没有任何 observation 输入，所以发布快照应该是一个稳定的空壳结构，
    // 这样 handler 即使比主链路更早启动，也不会返回缺字段的半成品 JSON。
    const ServiceRuntimeSnapshot snapshot = accumulator.BuildSnapshot();
    EXPECT_EQ(snapshot.overview.abnormal_service_count, 0U);
    EXPECT_EQ(snapshot.overview.abnormal_trace_count, 0U);
    EXPECT_EQ(snapshot.overview.latest_exception_time_ms, 0);
    EXPECT_TRUE(snapshot.services_topk.empty());
    EXPECT_TRUE(snapshot.global_operation_ranking.empty());
}

TEST(ServiceRuntimeAccumulatorTest, TickPublishesOverviewServicesAndGlobalOperationRanking)
{
    int64_t now_ms = 0;
    ServiceRuntimeAccumulator accumulator(/*service_top_k*/4,
                                         /*operation_top_k*/6,
                                         /*recent_sample_limit*/3,
                                         /*window_minutes*/30,
                                         [&now_ms]()
                                         {
                                             return now_ms;
                                         });

    accumulator.OnPrimaryCommitted(MakePrimaryObservation());
    now_ms = 60 * 1000;
    accumulator.OnTick();

    const ServiceRuntimeSnapshot snapshot = accumulator.BuildSnapshot();
    ASSERT_EQ(snapshot.overview.abnormal_service_count, 1U);
    ASSERT_EQ(snapshot.overview.abnormal_trace_count, 1U);
    ASSERT_EQ(snapshot.overview.latest_exception_time_ms, 1900);

    ASSERT_EQ(snapshot.services_topk.size(), 1U);
    EXPECT_EQ(snapshot.services_topk[0].service_name, "order-service");
    EXPECT_EQ(snapshot.services_topk[0].risk_level, "warning");
    EXPECT_EQ(snapshot.services_topk[0].exception_count, 1U);
    EXPECT_EQ(snapshot.services_topk[0].avg_latency_ms, 60);
    EXPECT_EQ(snapshot.services_topk[0].latest_exception_time_ms, 1900);

    ASSERT_EQ(snapshot.services_topk[0].operation_ranking.size(), 1U);
    EXPECT_EQ(snapshot.services_topk[0].operation_ranking[0].operation_name, "create-order");
    EXPECT_EQ(snapshot.services_topk[0].operation_ranking[0].count, 2U);
    EXPECT_EQ(snapshot.services_topk[0].operation_ranking[0].avg_latency_ms, 60);

    ASSERT_EQ(snapshot.global_operation_ranking.size(), 1U);
    EXPECT_EQ(snapshot.global_operation_ranking[0].service_name, "order-service");
    EXPECT_EQ(snapshot.global_operation_ranking[0].operation_name, "create-order");
    EXPECT_EQ(snapshot.global_operation_ranking[0].count, 2U);
}

TEST(ServiceRuntimeAccumulatorTest, TickOnlyPublishesSealedMinuteIntoWindow)
{
    int64_t now_ms = 0;
    ServiceRuntimeAccumulator accumulator(/*service_top_k*/4,
                                         /*operation_top_k*/6,
                                         /*recent_sample_limit*/3,
                                         /*window_minutes*/2,
                                         [&now_ms]()
                                         {
                                             return now_ms;
                                         });

    accumulator.OnPrimaryCommitted(MakePrimaryObservation());
    accumulator.OnTick();

    // 当前还停留在第 0 分钟，说明 observation 只是先记进“活跃分钟桶”，
    // 还没有形成已封口分钟，所以窗口统计此时不应该提前可见。
    ServiceRuntimeSnapshot snapshot = accumulator.BuildSnapshot();
    EXPECT_EQ(snapshot.overview.abnormal_service_count, 0U);
    EXPECT_EQ(snapshot.overview.abnormal_trace_count, 0U);
    EXPECT_TRUE(snapshot.services_topk.empty());
    EXPECT_TRUE(snapshot.global_operation_ranking.empty());

    now_ms = 60 * 1000;
    accumulator.OnTick();

    // 时间推进到下一分钟后，第 0 分钟才算封口并真正进入窗口。
    snapshot = accumulator.BuildSnapshot();
    ASSERT_EQ(snapshot.overview.abnormal_service_count, 1U);
    ASSERT_EQ(snapshot.overview.abnormal_trace_count, 1U);
    ASSERT_EQ(snapshot.services_topk.size(), 1U);
    ASSERT_EQ(snapshot.global_operation_ranking.size(), 1U);
}

TEST(ServiceRuntimeAccumulatorTest, TickEvictsExpiredMinuteFromWindow)
{
    int64_t now_ms = 0;
    ServiceRuntimeAccumulator accumulator(/*service_top_k*/4,
                                         /*operation_top_k*/6,
                                         /*recent_sample_limit*/3,
                                         /*window_minutes*/2,
                                         [&now_ms]()
                                         {
                                             return now_ms;
                                         });

    accumulator.OnPrimaryCommitted(MakePrimaryObservation());

    now_ms = 60 * 1000;
    accumulator.OnTick();
    ServiceRuntimeSnapshot snapshot = accumulator.BuildSnapshot();
    ASSERT_EQ(snapshot.overview.abnormal_trace_count, 1U);

    now_ms = 3 * 60 * 1000;
    accumulator.OnTick();

    // 2 分钟窗口下，minute=0 的数据在时间推进到 minute=3 时已经滑出窗口，
    // 所以 overview、服务榜和全局操作榜都应该退回空状态。
    snapshot = accumulator.BuildSnapshot();
    EXPECT_EQ(snapshot.overview.abnormal_service_count, 0U);
    EXPECT_EQ(snapshot.overview.abnormal_trace_count, 0U);
    EXPECT_TRUE(snapshot.services_topk.empty());
    EXPECT_TRUE(snapshot.global_operation_ranking.empty());
}

TEST(ServiceRuntimeAccumulatorTest, TickPublishesRecentSamplesAfterAnalysisReady)
{
    int64_t now_ms = 0;
    ServiceRuntimeAccumulator accumulator(/*service_top_k*/4,
                                         /*operation_top_k*/6,
                                         /*recent_sample_limit*/3,
                                         /*window_minutes*/30,
                                         [&now_ms]()
                                         {
                                             return now_ms;
                                         });

    accumulator.OnPrimaryCommitted(MakePrimaryObservation());
    accumulator.OnAnalysisReady(MakeAnalysisObservation());
    now_ms = 60 * 1000;
    accumulator.OnTick();

    const ServiceRuntimeSnapshot snapshot = accumulator.BuildSnapshot();
    ASSERT_EQ(snapshot.services_topk.size(), 1U);
    ASSERT_EQ(snapshot.services_topk[0].recent_samples.size(), 1U);
    EXPECT_EQ(snapshot.services_topk[0].recent_samples[0].trace_id, "trace-1");
    EXPECT_EQ(snapshot.services_topk[0].recent_samples[0].operation_name, "create-order");
    EXPECT_EQ(snapshot.services_topk[0].recent_samples[0].summary, "订单创建链路在库存确认阶段超时。");
    EXPECT_EQ(snapshot.services_topk[0].recent_samples[0].risk_level, "warning");
}
