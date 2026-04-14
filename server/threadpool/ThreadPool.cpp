#include <threadpool/ThreadPool.h>
#include "ThreadPool.h"
#include <MiniMuduo/base/LogMessage.h>
bool ThreadPool::submit(Task t)
{
    {
        std::unique_lock<std::mutex> mutex_(taskMutex_);
        if (stop_)
            return false;
        if(tasks_.size() < max_queue_size_)
        {
            tasks_.push(std::move(t));
            // 真正入队成功后才递增原子计数。
            // 这样 pendingTasks() 读到的就是“还没被 worker 取走”的真实队列长度，而不是近似值。
            pending_task_count_.fetch_add(1, std::memory_order_relaxed);
        }
        else 
            return false;
    }
    workCv_.notify_one();
    return true;
}

void ThreadPool::working()
{
    while (true)
    {
        Task task_;
        {
            {
                std::unique_lock<std::mutex> mutex_(taskMutex_);
                workCv_.wait(mutex_, [this]
                             { return !tasks_.empty() || stop_; });
                if(stop_&&tasks_.empty())
                    return;
                task_ = std::move(tasks_.front());
                tasks_.pop();
                // worker 一旦把任务从队列里摘走，它就不再属于 pending queue。
                // 这里立即递减原子计数，让背压读取看到的是“排队中的任务”，不是“排队+执行中”的混合数。
                pending_task_count_.fetch_sub(1, std::memory_order_relaxed);
            }
            try
            {
                task_();
            }
            catch (const std::exception &e)
            {
                LOG_STREAM_ERROR << "Work Thread catch Exception : " << e.what();
            }
        }
    }
}

void ThreadPool::shutdown()
{
    {
        std::lock_guard<std::mutex> mutex_(taskMutex_);
        stop_ = true;
    }
    workCv_.notify_all();
    for (std::thread &t : works_)
    {
        t.join();
    }
}
