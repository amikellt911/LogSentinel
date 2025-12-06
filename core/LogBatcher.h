#pragma once 
#include<vector>
#include<string>
#include<memory>
#include<mutex>
#include"core/AnalysisTask.h"

class ThreadPool;
class SqliteLogRepository;
class AiProvider;
class INotifier;

class LogBatcher
{
public:
    LogBatcher(ThreadPool* thread_pool,std::shared_ptr<SqliteLogRepository> repo,std::shared_ptr<AiProvider> ai,std::shared_ptr<INotifier> notifier);
    ~LogBatcher();
    // TODO(Optimization): 目前使用 std::mutex，MVP3阶段考虑换成 Spinlock + RingBuffer
    bool addLog(AnalysisTask task);
    void flush();
private:
    void processBatch(std::vector<AnalysisTask>);
private:
    ThreadPool* thread_pool_;
    std::shared_ptr<SqliteLogRepository> repo_;
    std::shared_ptr<AiProvider> ai_;
    std::shared_ptr<INotifier> notifier_;
    std::mutex mutex_;
    std::vector<AnalysisTask> buffer_;
    size_t buffer_size_ = 100;
};

