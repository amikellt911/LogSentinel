#pragma once

#include <string>
#include "persistence/TraceRepository.h"

// SqliteTraceRepository 作为 SQLite 版本的 Trace 存储占位实现，后续再接入真实 SQL 逻辑。
class SqliteTraceRepository : public TraceRepository
{
public:
    explicit SqliteTraceRepository(const std::string& db_path);

    bool SaveTraceSummary(const TraceSummary& summary) override;
    bool SaveTraceSpans(const std::string& trace_id, const std::vector<TraceSpanRecord>& spans) override;
    bool SaveTraceAnalysis(const TraceAnalysisRecord& analysis) override;
    bool SavePromptDebug(const PromptDebugRecord& record) override;

private:
    std::string db_path_;
};
