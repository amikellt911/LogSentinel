#include"core/LogBatcher.h"
#include "LogBatcher.h"
#include "threadpool/ThreadPool.h"
#include "ai/AiProvider.h"
#include "notification/INotifier.h"
#include "persistence/SqliteLogRepository.h"
#include<iostream>
LogBatcher::LogBatcher(ThreadPool* thread_pool,std::shared_ptr<SqliteLogRepository> repo,std::shared_ptr<AiProvider> ai_client,std::shared_ptr<INotifier> notifier)
:thread_pool_(thread_pool),repo_(repo),ai_client_(ai_client),notifier_(notifier)
{
    ring_buffer_.resize(capacity_);
}

bool LogBatcher::push(AnalysisTask &&task)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(count_ >= capacity_)
    return false;
    ring_buffer_[tail_]=std::move(task);
    tail_=(tail_+1)%capacity_;
    count_++;
    if(count_>=batch_size_)
    {
            tryDispatch();
    }
    return true;
}
void LogBatcher::tryDispatch()
{
    //好像不需要锁，因为外面push的时候已经锁了
    //std::lock_guard<std::mutex> lock(mutex_);
    if(thread_pool_->pendingTasks()>=pool_threshold_)
        return;
    std::vector<AnalysisTask> batch(batch_size_);
    batch.reserve(batch_size_);
    for(size_t i = 0; i < batch_size_; i++)
    {
        //emplace_back是直接用参数构造到vector的空间，不需要构造临时对象后再拷贝到vector的空间。
        batch.push_back(std::move(ring_buffer_[head_]));
        head_=(head_+1)%capacity_;
    }
    count_ -= batch_size_;
    thread_pool_->submit([this,batch=std::move(batch)]() mutable{
        this->processBatch(std::move(batch));
    });
}
void LogBatcher::processBatch(std::vector<AnalysisTask> batch)
{
        
}
