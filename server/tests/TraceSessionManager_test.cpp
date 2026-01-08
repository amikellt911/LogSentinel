#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "core/TraceSessionManager.h"

class TraceSessionManagerTest : public ::testing::Test
{
protected:
    SpanEvent MakeSpan(size_t trace_key, size_t span_id, int64_t start_time_ms)
    {
        SpanEvent span;
        span.trace_key = trace_key;
        span.span_id = span_id;
        span.start_time_ms = start_time_ms;
        span.name = "span";
        span.service_name = "service";
        return span;
    }
};

TEST_F(TraceSessionManagerTest, PushCreatesSession)
{
    TraceSessionManager manager(nullptr, nullptr, nullptr, 10, 1000);
    SpanEvent span = MakeSpan(1, 100, 10);

    manager.Push(span);

    EXPECT_EQ(manager.size(), 1u);
}

TEST_F(TraceSessionManagerTest, DuplicateSpanTriggersDispatch)
{
    TraceSessionManager manager(nullptr, nullptr, nullptr, 10, 1000);
    SpanEvent span = MakeSpan(1, 100, 10);

    manager.Push(span);
    manager.Push(span);

    EXPECT_EQ(manager.size(), 0u);
}

TEST_F(TraceSessionManagerTest, CapacityTriggersDispatch)
{
    TraceSessionManager manager(nullptr, nullptr, nullptr, 2, 1000);
    SpanEvent span1 = MakeSpan(1, 100, 10);
    SpanEvent span2 = MakeSpan(1, 101, 20);

    manager.Push(span1);
    manager.Push(span2);

    EXPECT_EQ(manager.size(), 0u);
}

TEST_F(TraceSessionManagerTest, TraceEndTriggersDispatch)
{
    TraceSessionManager manager(nullptr, nullptr, nullptr, 10, 1000);
    SpanEvent span = MakeSpan(1, 100, 10);
    span.trace_end = true;

    manager.Push(span);

    EXPECT_EQ(manager.size(), 0u);
}

TEST_F(TraceSessionManagerTest, BuildTraceIndexRootsAndChildren)
{
    TraceSessionManager manager(nullptr, nullptr, nullptr, 10, 1000);
    TraceSession session(10, 1000);
    session.trace_key = 1;

    SpanEvent root = MakeSpan(1, 1, 10);
    SpanEvent child = MakeSpan(1, 2, 20);
    child.parent_span_id = 1;
    SpanEvent orphan = MakeSpan(1, 3, 30);
    orphan.parent_span_id = 99;

    session.spans.push_back(root);
    session.spans.push_back(child);
    session.spans.push_back(orphan);

    TraceSessionManager::TraceIndex index = manager.BuildTraceIndex(session);

    ASSERT_EQ(index.roots.size(), 2u);
    EXPECT_EQ(index.children[1].size(), 1u);
    EXPECT_EQ(index.children[1][0], 2u);
}

TEST_F(TraceSessionManagerTest, SerializeTraceSortsChildrenByStartTime)
{
    TraceSessionManager manager(nullptr, nullptr, nullptr, 10, 1000);
    TraceSession session(10, 1000);
    session.trace_key = 1;

    SpanEvent root = MakeSpan(1, 1, 10);
    SpanEvent child_late = MakeSpan(1, 2, 200);
    child_late.parent_span_id = 1;
    SpanEvent child_early = MakeSpan(1, 3, 100);
    child_early.parent_span_id = 1;

    session.spans.push_back(root);
    session.spans.push_back(child_late);
    session.spans.push_back(child_early);

    TraceSessionManager::TraceIndex index = manager.BuildTraceIndex(session);
    std::vector<const SpanEvent*> order;
    std::string payload = manager.SerializeTrace(index, &order);
    nlohmann::json parsed = nlohmann::json::parse(payload);

    ASSERT_TRUE(parsed.contains("spans"));
    ASSERT_EQ(parsed["spans"].size(), 1u);
    ASSERT_EQ(parsed["spans"][0]["children"].size(), 2u);
    EXPECT_EQ(parsed["spans"][0]["children"][0]["span_id"], 3u);
    EXPECT_EQ(parsed["spans"][0]["children"][1]["span_id"], 2u);
}
