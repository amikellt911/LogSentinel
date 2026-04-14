#pragma once
#include<vector>
#include<functional>
#include<thread>
#include<queue>
#include<atomic>
#include<mutex>
#include<condition_variable>
class ThreadPool{
    public:
        explicit ThreadPool(size_t threadNums,size_t max_queue_size=10000)
        {
            max_queue_size_=max_queue_size;
            for(size_t i=0;i<threadNums;i++)
            {
                works_.emplace_back([this]{this->working();});
            }
        }
        ~ThreadPool()
        {
            if(!stop_)
                shutdown();
        }
        using Task=std::function<void()>;
        bool submit(Task t);
        void shutdown();
        size_t pendingTasks() const{
            // 队列待处理数是背压热路径会频繁读取的指标。
            // 这里直接返回原子计数，避免 TraceSessionManager 每次刷门禁都要再抢一遍 taskMutex_。
            return pending_task_count_.load(std::memory_order_relaxed);
        }
        size_t maxQueueSize() const{
            return max_queue_size_;
        }
    private:
        void working();
        std::vector<std::thread> works_;
        std::queue<Task> tasks_;
        size_t max_queue_size_;
        // tasks_ 仍然是真实队列本体，受 taskMutex_ 保护。
        // pending_task_count_ 只是给热路径读取准备的原子计数，不承担队列所有权，也不替代互斥锁。
        std::atomic<size_t> pending_task_count_{0};

        std::mutex taskMutex_;
        std::condition_variable workCv_;
        bool stop_=false;


};
