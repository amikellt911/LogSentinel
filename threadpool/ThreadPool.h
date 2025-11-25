#pragma once
#include<vector>
#include<functional>
#include<thread>
#include<queue>
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
    private:
        void working();
        std::vector<std::thread> works_;
        std::queue<Task> tasks_;
        size_t max_queue_size_;

        std::mutex taskMutex_;
        std::condition_variable workCv_;
        bool stop_=false;


};