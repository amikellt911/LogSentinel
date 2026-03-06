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
            tasks_.push(std::move(t));
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