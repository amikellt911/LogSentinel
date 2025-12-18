#include "util/TraceIdGenerator.h"
#include <gtest/gtest.h>
#include <regex>
#include <set>
#include <thread>
#include <vector>
#include <mutex>
#include <sstream>

// Define a test fixture class to match the project's style, even if it's empty.
class TraceIdGeneratorTest : public ::testing::Test {};

// Helper function to split string for testing
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Test case 1: Check if the generated Trace ID has the correct format.
TEST_F(TraceIdGeneratorTest, Format) {
    std::string traceId = generateTraceId();
    std::regex format_regex("^[0-9a-f]+-[0-9a-f]+-[0-9a-f]+-[0-9a-f]+-[0-9a-f]+$");
    EXPECT_TRUE(std::regex_match(traceId, format_regex));
}

// Test case 2: Check for uniqueness in a single thread.
TEST_F(TraceIdGeneratorTest, SingleThreadUniqueness) {
    const int num_ids = 10000;
    std::set<std::string> ids;
    for (int i = 0; i < num_ids; ++i) {
        ids.insert(generateTraceId());
    }
    ASSERT_EQ(ids.size(), num_ids);
}

// Test case 3: Check for uniqueness across multiple threads.
TEST_F(TraceIdGeneratorTest, MultiThreadUniqueness) {
    const int num_threads = 10;
    const int ids_per_thread = 1000;
    std::vector<std::thread> threads;
    std::vector<std::string> generated_ids;
    std::mutex mtx;

    auto generate_ids_task = [&](int count) {
        std::vector<std::string> local_ids;
        local_ids.reserve(count);
        for (int i = 0; i < count; ++i) {
            local_ids.push_back(generateTraceId());
        }
        std::lock_guard<std::mutex> lock(mtx);
        generated_ids.insert(generated_ids.end(), local_ids.begin(), local_ids.end());
    };

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(generate_ids_task, ids_per_thread);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::set<std::string> unique_ids(generated_ids.begin(), generated_ids.end());
    ASSERT_EQ(unique_ids.size(), num_threads * ids_per_thread);
}

// Test case 4: Check if the counter part increments correctly in the same microsecond.
TEST_F(TraceIdGeneratorTest, CounterIncrement) {
    bool test_passed = false;
    // Retry a few times to get two IDs within the same microsecond, which is highly likely.
    for(int i = 0; i < 100; ++i) { 
        std::string id1 = generateTraceId();
        std::string id2 = generateTraceId();

        auto parts1 = split(id1, '-');
        auto parts2 = split(id2, '-');

        ASSERT_EQ(parts1.size(), 5);
        ASSERT_EQ(parts2.size(), 5);

        // If timestamp (parts[0]) is the same, the counter (parts[4]) should increment by 1.
        if (parts1[0] == parts2[0]) {
            // The other parts should also be identical.
            EXPECT_EQ(parts1[1], parts2[1]); // host_id
            EXPECT_EQ(parts1[2], parts2[2]); // p_id
            EXPECT_EQ(parts1[3], parts2[3]); // t_id

            unsigned long long counter1 = std::stoull(parts1[4], nullptr, 16);
            unsigned long long counter2 = std::stoull(parts2[4], nullptr, 16);
            EXPECT_EQ(counter2, counter1 + 1);
            
            test_passed = true;
            break; // Test passed, no need to retry.
        }
    }
    
    if (!test_passed) {
        GTEST_SKIP() << "Could not get two consecutive IDs with the same timestamp. Skipping test.";
    }
}
