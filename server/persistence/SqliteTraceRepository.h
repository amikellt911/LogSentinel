#pragma once

#include <mutex>
#include <string>
#include "persistence/TraceRepository.h"

struct sqlite3;

// SqliteTraceRepository 作为 SQLite 版本的 Trace 存储占位实现，后续再接入真实 SQL 逻辑。
class SqliteTraceRepository : public TraceRepository
{
public:
    explicit SqliteTraceRepository(const std::string& db_path);
    ~SqliteTraceRepository();

    bool SaveSingleTraceSummary(const TraceSummary& summary) override;
    bool SaveSingleTraceSpans(const std::string& trace_id, const std::vector<TraceSpanRecord>& spans) override;
    bool SaveSingleTraceAnalysis(const TraceAnalysisRecord& analysis) override;
    bool SaveSinglePromptDebug(const PromptDebugRecord& record) override;
    bool SaveSingleTraceAtomic(const TraceSummary& summary,
                        const std::vector<TraceSpanRecord>& spans,
                        const TraceAnalysisRecord* analysis,
                        const PromptDebugRecord* prompt_debug) override;

private:
    std::string db_path_;
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};
