#include"core/LogBatcher.h"
#include "LogBatcher.h"
#include "threadpool/ThreadPool.h"
#include "ai/AiProvider.h"
#include "notification/INotifier.h"
#include "persistence/SqliteLogRepository.h"
#include<iostream>
#include"MiniMuduo/net/EventLoop.h"
LogBatcher::LogBatcher(ThreadPool* thread_pool,std::shared_ptr<SqliteLogRepository> repo,std::shared_ptr<AiProvider> ai_client,std::shared_ptr<INotifier> notifier)
:thread_pool_(thread_pool),repo_(repo),ai_client_(ai_client),notifier_(notifier)
{
    ring_buffer_.resize(capacity_);
}
LogBatcher::LogBatcher(MiniMuduo::net::EventLoop* loop,ThreadPool* thread_pool,std::shared_ptr<SqliteLogRepository> repo,std::shared_ptr<AiProvider> ai_client,std::shared_ptr<INotifier> notifier)
:loop_(loop),thread_pool_(thread_pool),repo_(repo),ai_client_(ai_client),notifier_(notifier)
{
    ring_buffer_.resize(capacity_);
    loop_->runEvery(0.5,[this](){this->onTimeout();});
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
        tryDispatch(batch_size_);
    }
    return true;
}
// [设计决策] 采用“定期巡检”而非“推迟重置”
// 1. 避免系统调用风暴：高并发下频繁 reset 会导致大量 timerfd_settime 系统调用
//    和用户态/内核态切换，这是严重的性能杀手。
// 2. 负载无关性：runEvery 的开销是固定的（每秒 n 次），不会随 QPS 线性增长，
//    配合 Mutex 的双重检查机制，是目前最稳健的工业级实践。
//每次只定时，不reset，是因为频繁定时器重置，会频繁系统调用和用户系统态切换，耗时大
//主要是高负载情况下，可能10ms就满了，到batch_size_了，结果频繁重置定时器，就会导致开销大
//短负载也有mutex，所以可行，是最靠谱的工业实践
void LogBatcher::onTimeout(){
    std::lock_guard<std::mutex> lock(mutex_);
    if(count_>0){
        size_t limit=std::min(count_,batch_size_);
        tryDispatch(limit);
    }
}
void LogBatcher::tryDispatch(size_t limit)
{
    //好像不需要锁，因为外面push的时候已经锁了
    //std::lock_guard<std::mutex> lock(mutex_);
    if(thread_pool_->pendingTasks()>=pool_threshold_)
        return;
    std::vector<AnalysisTask> batch;
    batch.reserve(limit);
    for(size_t i = 0; i < batch_size_; i++)
    {
        //emplace_back是直接用参数构造到vector的空间，不需要构造临时对象后再拷贝到vector的空间。
        batch.push_back(std::move(ring_buffer_[head_]));
        head_=(head_+1)%capacity_;
    }
    count_ -= limit;
    thread_pool_->submit([this,batch=std::move(batch)]() mutable{
        this->processBatch(std::move(batch));
    });
}
void LogBatcher::processBatch(std::vector<AnalysisTask> batch)
{
        
}
