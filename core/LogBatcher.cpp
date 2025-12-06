#include"core/LogBatcher.h"
#include "LogBatcher.h"

LogBatcher::LogBatcher(ThreadPool* thread_pool,std::shared_ptr<SqliteLogRepository> repo,std::shared_ptr<AiProvider> ai,std::shared_ptr<INotifier> notifier)
:thread_pool_(thread_pool),repo_(repo),ai_(ai),notifier_(notifier)
{
    ring_buffer_.reserve(capacity_);
}

bool LogBatcher::push(AnalysisTask &&task)
{
    return false;
}
void LogBatcher::tryDispatch()
{
}
void LogBatcher::processBatch(std::vector<AnalysisTask> batch)
{
    // 占位实现
}
