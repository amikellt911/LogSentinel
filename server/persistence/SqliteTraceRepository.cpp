#include "persistence/SqliteTraceRepository.h"

SqliteTraceRepository::SqliteTraceRepository(const std::string& db_path)
    : db_path_(db_path)
{
}

bool SqliteTraceRepository::SaveTraceSummary(const TraceSummary& summary)
{
    // 仅占位，后续接入 SQLite 写入逻辑。
    (void)summary;
    return false;
}

bool SqliteTraceRepository::SaveTraceSpans(const std::string& trace_id, const std::vector<TraceSpanRecord>& spans)
{
    // 仅占位，后续接入 SQLite 写入逻辑。
    (void)trace_id;
    (void)spans;
    return false;
}

bool SqliteTraceRepository::SaveTraceAnalysis(const TraceAnalysisRecord& analysis)
{
    // 仅占位，后续接入 SQLite 写入逻辑。
    (void)analysis;
    return false;
}

bool SqliteTraceRepository::SavePromptDebug(const PromptDebugRecord& record)
{
    // 仅占位，后续接入 SQLite 写入逻辑。
    (void)record;
    return false;
}
