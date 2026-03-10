#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "persistence/TraceRepository.h"

// BufferedTraceRepository 不是底层 Repository 的替身，它更像一个“前面一层的缓冲写入器”。
// 既然当前持久化时间线已经拆成：
// 1) Dispatch 前就能拿到 summary + spans
// 2) AI 返回后才拿到 analysis + prompt_debug
// 那这一层就只暴露两个 append 入口，内部再持有真正的 TraceRepository sink。
class BufferedTraceRepository
{
public:
    using TraceSummary = TraceRepository::TraceSummary;
    using TraceSpanRecord = TraceRepository::TraceSpanRecord;
    using TraceAnalysisRecord = TraceRepository::TraceAnalysisRecord;
    using PromptDebugRecord = TraceRepository::PromptDebugRecord;

    struct Config
    {
        size_t primary_summary_reserve = 64;
        size_t primary_span_reserve = 512;
        size_t analysis_reserve = 64;
        size_t prompt_debug_reserve = 64;
        size_t initial_buffer_count = 4;
        int64_t primary_flush_interval_ms = 200;
        int64_t analysis_flush_interval_ms = 500;
    };

    struct PrimaryBufferGroup
    {
        std::vector<TraceSummary> summaries;
        std::vector<TraceSpanRecord> spans;
        int64_t first_enqueue_ms = 0;

        bool Empty() const
        {
            return summaries.empty() && spans.empty();
        }

        void ClearButKeepCapacity()
        {
            summaries.clear();
            spans.clear();
            first_enqueue_ms = 0;
        }
    };

    struct AnalysisBufferGroup
    {
        std::vector<TraceAnalysisRecord> analyses;
        std::vector<PromptDebugRecord> prompt_debugs;
        int64_t first_enqueue_ms = 0;

        bool Empty() const
        {
            return analyses.empty() && prompt_debugs.empty();
        }

        void ClearButKeepCapacity()
        {
            analyses.clear();
            prompt_debugs.clear();
            first_enqueue_ms = 0;
        }
    };

    using TracePrimaryWrite = TraceRepository::TracePrimaryWrite;
    using TraceAnalysisWrite = TraceRepository::TraceAnalysisWrite;

    explicit BufferedTraceRepository(std::shared_ptr<TraceRepository> sink);
    BufferedTraceRepository(std::shared_ptr<TraceRepository> sink, Config config);
    ~BufferedTraceRepository();

    bool AppendPrimary(TracePrimaryWrite write);
    bool AppendAnalysis(TraceAnalysisWrite write);

private:
    using PrimaryBufferPtr = std::unique_ptr<PrimaryBufferGroup>;
    using AnalysisBufferPtr = std::unique_ptr<AnalysisBufferGroup>;

    PrimaryBufferPtr CreatePrimaryBuffer() const;
    AnalysisBufferPtr CreateAnalysisBuffer() const;
    void FlushLoop();
    void StopFlushThread();

    std::shared_ptr<TraceRepository> sink_;
    Config config_;

    std::mutex primary_mutex_;
    std::condition_variable primary_cv_;
    PrimaryBufferPtr current_primary_;
    PrimaryBufferPtr next_primary_;
    std::vector<PrimaryBufferPtr> free_primary_buffers_;
    std::vector<PrimaryBufferPtr> full_primary_buffers_;

    std::mutex analysis_mutex_;
    std::condition_variable analysis_cv_;
    AnalysisBufferPtr current_analysis_;
    AnalysisBufferPtr next_analysis_;
    std::vector<AnalysisBufferPtr> free_analysis_buffers_;
    std::vector<AnalysisBufferPtr> full_analysis_buffers_;

    std::mutex state_mutex_;
    std::condition_variable flush_cv_;
    bool stopping_ = false;
    std::thread flush_thread_;
};
