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
    LogBatcher(ThreadPool* thread_pool,std::shared_ptr<SqliteLogRepository> repo,std::shared_ptr<AiProvider> ai_client_,std::shared_ptr<INotifier> notifier);
    ~LogBatcher();
    bool push(AnalysisTask&& task);
private:
    void tryDispatch();
    void processBatch(std::vector<AnalysisTask> batch);
private:
    ThreadPool* thread_pool_;
    std::shared_ptr<SqliteLogRepository> repo_;
    std::shared_ptr<AiProvider> ai_client_;
    std::shared_ptr<INotifier> notifier_;
    // --- 核心：Ring Buffer 实现 ---
    std::vector<AnalysisTask> ring_buffer_; 
    size_t capacity_ = 10000; // 也是硬上限
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;        // 当前积压数量

    std::mutex mutex_;
    // 流控参数
    size_t batch_size_ = 100;    // 攒够多少发车
    size_t pool_threshold_ = 50; // 下游堵了就不发
};

