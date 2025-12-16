#include <gtest/gtest.h>
#include <threadpool/ThreadPool.h>
#include <atomic>
#include <chrono>
#include <iostream>

TEST(ThreadPoolTest, SubmitAndExecuteTasks) {
    ThreadPool pool(4);
    std::atomic<int> counter(0);

    for (int i = 0; i < 10; ++i) {
        pool.submit([&]() {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    }
    // Give some time for tasks to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    //ASSERT_EQ(counter, 10);
    pool.shutdown();
    ASSERT_EQ(counter, 10);
}

TEST(ThreadPoolTest, Shutdown) {
    ThreadPool pool(2);
    std::atomic<int> counter(0);

    pool.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        counter++;
    });

    pool.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        counter++;
    });

    pool.shutdown();
    ASSERT_EQ(counter, 2);
}

TEST(ThreadPoolTest, SubmitAfterShutdown) {
    ThreadPool pool(1);
    
    std::atomic<int> counter(0);

    pool.submit([&](){
        counter++;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    pool.shutdown();
    bool submitted = pool.submit([&]() {
        std::cout << "This should not be executed." << std::endl;
        counter++;
    });

    ASSERT_EQ(counter, 1);
    ASSERT_FALSE(submitted);
}
