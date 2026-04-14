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

TEST(ThreadPoolTest, PendingTasksTracksQueuedButNotRunningTasks) {
    ThreadPool pool(1, 8);
    std::mutex mutex;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    std::atomic<bool> second_started(false);

    ASSERT_TRUE(pool.submit([&]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            // 第一条任务故意卡住 worker，目的是把第二条任务稳定留在队列里，
            // 这样 pendingTasks() 读到的就必须是“排队中”而不是“排队+执行中”的混合数。
            first_started = true;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return release_first; });
    }));

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::milliseconds(500), [&]() { return first_started; });
    }

    // 第一条任务已经被 worker 摘走开始执行，因此 pending queue 现在应该是空的。
    EXPECT_EQ(pool.pendingTasks(), 0u);

    ASSERT_TRUE(pool.submit([&]() {
        second_started.store(true, std::memory_order_release);
    }));

    EXPECT_EQ(pool.pendingTasks(), 1u);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first = true;
    }
    cv.notify_all();

    for (int i = 0; i < 50; ++i) {
        if (second_started.load(std::memory_order_acquire)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(second_started.load(std::memory_order_acquire));
    EXPECT_EQ(pool.pendingTasks(), 0u);

    pool.shutdown();
}
