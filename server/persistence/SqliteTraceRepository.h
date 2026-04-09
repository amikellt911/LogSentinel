#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "persistence/TraceRepository.h"

struct sqlite3;

// 读侧查询类型先放在 SQLite Trace Repo 头文件里，后续如果查询面继续扩大，
// 再单独抽成 TraceQueryTypes.h，避免当前阶段为了“抽象整齐”先拆过多文件。
struct TraceSearchRequest
{
    std::optional<std::string> trace_id;
    // 当前语义固定为“入口服务”，不是“任意参与过的服务”。
    std::optional<std::string> service_name;
    std::optional<int64_t> start_time_ms;
    std::optional<int64_t> end_time_ms;
    std::vector<std::string> risk_levels;
    size_t page = 1;
    size_t page_size = 20;
};

struct TraceListItem
{
    std::string trace_id;
    std::string service_name;
    int64_t start_time_ms = 0;
    std::optional<int64_t> end_time_ms;
    int64_t duration_ms = 0;
    size_t span_count = 0;
    size_t token_count = 0;
    std::string risk_level;
};

struct TraceSearchResult
{
    size_t total = 0;
    std::vector<TraceListItem> items;
};

struct TraceSpanDetail
{
    std::string span_id;
    std::optional<std::string> parent_id;
    std::string service_name;
    std::string operation;
    int64_t start_time_ms = 0;
    int64_t duration_ms = 0;
    std::string raw_status;
};

struct TraceAnalysisDetail
{
    std::string risk_level;
    std::string summary;
    std::string root_cause;
    std::string solution;
    double confidence = 0.0;
};

struct TraceDetailRecord
{
    std::string trace_id;
    std::string service_name;
    int64_t start_time_ms = 0;
    std::optional<int64_t> end_time_ms;
    int64_t duration_ms = 0;
    size_t span_count = 0;
    size_t token_count = 0;
    std::string risk_level;
    std::vector<std::string> tags;
    std::optional<TraceAnalysisDetail> analysis;
    std::vector<TraceSpanDetail> spans;
};

// SqliteTraceRepository 作为 SQLite 版本的 Trace 存储占位实现，后续再接入真实 SQL 逻辑。
class SqliteTraceRepository : public TraceRepository
{
public:
    explicit SqliteTraceRepository(const std::string& db_path);
    ~SqliteTraceRepository();

    bool SaveSingleTraceSummary(const TraceSummary& summary) override;
    bool SaveSingleTraceSpans(const std::string& trace_id, const std::vector<TraceSpanRecord>& spans) override;
    bool SaveSingleTraceAnalysis(const TraceAnalysisRecord& analysis) override;
    bool SaveSingleTraceAtomic(const TraceSummary& summary,
                        const std::vector<TraceSpanRecord>& spans,
                        const TraceAnalysisRecord* analysis) override;
    bool SavePrimaryBatch(const std::vector<TraceSummary>& summaries,
                          const std::vector<TraceSpanRecord>& spans) override;
    bool SaveAnalysisBatch(const std::vector<TraceAnalysisRecord>& analyses) override;

    // 读侧先只暴露两个最小入口：列表搜索 + 单条详情。
    // 当前阶段先直接落在 SQLite 仓库里，不急着上升到抽象基类，
    // 否则为了未来可能不会发生的“多存储读实现”先加一层，会把本轮节奏拖慢。
    TraceSearchResult SearchTraces(const TraceSearchRequest& request);
    std::optional<TraceDetailRecord> GetTraceDetail(const std::string& trace_id);
    bool DeleteTraceById(const std::string& trace_id);
    size_t DeleteExpiredTracesBatch(int64_t cutoff_ms, size_t limit);

private:
    bool DeleteTracesByIdsAtomic(const std::vector<std::string>& trace_ids);

    std::string db_path_;
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};
