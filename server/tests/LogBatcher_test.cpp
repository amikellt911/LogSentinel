#include <gtest/gtest.h>
#include <memory>
#include <filesystem>
#include "core/LogBatcher.h"
#include "persistence/SqliteLogRepository.h"
#include "persistence/SqliteConfigRepository.h"
#include "ai/MockAI.h"
#include "notification/WebhookNotifier.h"
#include "threadpool/ThreadPool.h"
#include "MiniMuduo/net/EventLoop.h"

// 使用 Test Fixture (TEST_F) 来管理繁琐的初始化
class LogBatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 1. 准备 DB (每次测试用独立的 DB 文件，防止干扰)
        test_db_path = "test_batcher_" + std::to_string(std::time(nullptr)) + ".db";
        repo = std::make_shared<SqliteLogRepository>(test_db_path);

        // 1.5 准备 Config Repo (复用 DB 路径)
        config_repo = std::make_shared<SqliteConfigRepository>(test_db_path);

        // 2. 准备 Mock 组件
        ai = std::make_shared<MockAI>();
        notifier = std::make_shared<WebhookNotifier>(std::vector<std::string>{});
        
        // 3. 准备线程池 (给 2 个线程够用了)
        tpool = std::make_unique<ThreadPool>(2);

        // 4. 创建被测对象 Batcher
        // 注意：传入 &loop
        batcher = std::make_shared<LogBatcher>(&loop, tpool.get(), repo, ai, notifier, config_repo);
    }

    void TearDown() override {
        // 清理 DB 文件
        std::filesystem::remove(test_db_path);
        std::filesystem::remove(test_db_path + "-shm"); // WAL 模式可能有这些
        std::filesystem::remove(test_db_path + "-wal");
    }

    // 辅助函数：等待异步操作完成
    // 原理：运行 loop 一段时间，让定时器和回调有机会执行
    void wait(double seconds) {
        loop.runAfter(seconds, [this]() { 
            loop.quit(); 
        });
        loop.loop(); // 这会阻塞，直到 runAfter 的回调执行 quit
    }

protected:
    MiniMuduo::net::EventLoop loop; // 局部的 EventLoop
    std::unique_ptr<ThreadPool> tpool;
    std::shared_ptr<SqliteLogRepository> repo;
    std::shared_ptr<SqliteConfigRepository> config_repo;
    std::shared_ptr<AiProvider> ai;
    std::shared_ptr<WebhookNotifier> notifier;
    std::shared_ptr<LogBatcher> batcher;
    std::string test_db_path;
};

// 场景 1: 测试满载触发 (Batch Size Trigger)
TEST_F(LogBatcherTest, FlushWhenFull) {
    // 假设 batcher 默认 batch_size 是 100
    // 我们没法轻易改 private 的 batch_size，除非加 setter 或者用反射
    // 这里我们直接推 100 条 (稍微有点多，但能测)
    // 或者你可以为了测试，给 LogBatcher 加一个 setBatchSize 接口（推荐）
    
    // 假设我们不想改代码，硬推 100 条
    int batch_size = 100;
    
    for (int i = 0; i < batch_size; ++i) {
        AnalysisTask task;
        task.trace_id = "trace_" + std::to_string(i);
        task.raw_request_body = "log content";
        task.start_time = std::chrono::steady_clock::now();
        EXPECT_TRUE(batcher->push(std::move(task)));
    }

    // 此时应该触发了 flush，任务被提交到了 ThreadPool
    // 我们等待一小会儿让 Worker 线程写 DB
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    // 验证 DB 里是否有 100 条记录
    // 注意：这需要 SqliteLogRepository 有查询数量的接口，或者我们查一条试试
    auto res = repo->queryStructResultByTraceId("trace_99"); // 查最后一条
    EXPECT_TRUE(res.has_value());
}

// 场景 2: 测试超时触发 (Timeout Trigger)
TEST_F(LogBatcherTest, FlushOnTimeout) {
    // 只推 1 条，肯定没满
    AnalysisTask task;
    task.trace_id = "trace_timeout";
    task.raw_request_body = "timeout test";
    task.start_time = std::chrono::steady_clock::now();
    batcher->push(std::move(task));

    // 此时如果不等待，直接查肯定是空的
    auto res_immediate = repo->queryStructResultByTraceId("trace_timeout");
    EXPECT_FALSE(res_immediate.has_value());

    // 运行 EventLoop 1.5 秒 (Batcher 的定时器是 0.5s)
    wait(3);

    // 再次查询，应该有了
    auto res_after = repo->queryStructResultByTraceId("trace_timeout");
    EXPECT_TRUE(res_after.has_value());
}

// 场景 3: 测试拒绝策略 (Overflow)
TEST_F(LogBatcherTest, RejectWhenFull) {
    // Batcher 默认 capacity 是 10000
    // 要测这个太慢了，建议给 LogBatcher 加一个 setCapacity(size_t) 接口用于测试
    // 这里演示原理，假设 capacity 被设为了 5

    batcher->setCapacityForTest(5);
    
    for(int i=0; i<5; ++i) {
        AnalysisTask t; t.trace_id = std::to_string(i);
        EXPECT_TRUE(batcher->push(std::move(t)));
    }
    
    AnalysisTask t_fail;
    t_fail.trace_id = "fail";
    EXPECT_FALSE(batcher->push(std::move(t_fail))); // 应该返回 false

}