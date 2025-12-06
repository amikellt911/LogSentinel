#include"core/LogBatcher.h"

LogBatcher::LogBatcher(ThreadPool* thread_pool,std::shared_ptr<SqliteLogRepository> repo,std::shared_ptr<AiProvider> ai,std::shared_ptr<INotifier> notifier)
:thread_pool_(thread_pool),repo_(repo),ai_(ai),notifier_(notifier)
{
    buffer_.reserve(150);
}

LogBatcher::~LogBatcher()
{
}

bool LogBatcher::addLog(AnalysisTask task) {
    // 占位实现
    return true;
}

void LogBatcher::flush() {
    // 占位实现
}

void LogBatcher::processBatch(std::vector<AnalysisTask> batch) {
    // 占位实现
}
