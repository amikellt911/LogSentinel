#include <gtest/gtest.h>
#include <persistence/SqliteConfigRepository.h>
#include <filesystem>
#include <fstream>

// Use a distinct database file for testing
const std::string TEST_DB_PATH = "test_config_repo.db";

class SqliteConfigRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure a clean state before each test
        cleanup();
    }

    void TearDown() override {
        // Cleanup after each test
        cleanup();
    }

    void cleanup() {
        if (std::filesystem::exists(TEST_DB_PATH)) {
            std::filesystem::remove(TEST_DB_PATH);
        }
        // Also remove the wal/shm files if they exist
        if (std::filesystem::exists(TEST_DB_PATH + "-wal")) {
            std::filesystem::remove(TEST_DB_PATH + "-wal");
        }
        if (std::filesystem::exists(TEST_DB_PATH + "-shm")) {
            std::filesystem::remove(TEST_DB_PATH + "-shm");
        }
    }
};

TEST_F(SqliteConfigRepositoryTest, InitAndDefaultValues) {
    SqliteConfigRepository repo(TEST_DB_PATH);

    // Verify default app config values are loaded (as defined in the constructor SQL)
    AppConfig config = repo.getAppConfig();
    EXPECT_EQ(config.ai_provider, "openai");
    EXPECT_EQ(config.kernel_worker_threads, 4);

    // Verify prompts are empty or contain defaults if any (current code doesn't insert default prompts)
    auto prompts = repo.getAllPrompts();
    EXPECT_TRUE(prompts.empty());

    // Verify channels are empty
    auto channels = repo.getAllChannels();
    EXPECT_TRUE(channels.empty());
}

TEST_F(SqliteConfigRepositoryTest, UpdateAppConfig) {
    {
        SqliteConfigRepository repo(TEST_DB_PATH);
        std::map<std::string, std::string> updates;
        updates["ai_provider"] = "gemini";
        updates["kernel_worker_threads"] = "8";

        repo.handleUpdateAppConfig(updates);

        // Check cache update immediately
        AppConfig config = repo.getAppConfig();
        EXPECT_EQ(config.ai_provider, "gemini");
        EXPECT_EQ(config.kernel_worker_threads, 8);
    }

    // Reload from DB to check persistence
    {
        SqliteConfigRepository repo(TEST_DB_PATH);
        AppConfig config = repo.getAppConfig();
        EXPECT_EQ(config.ai_provider, "gemini");
        EXPECT_EQ(config.kernel_worker_threads, 8);
    }
}

TEST_F(SqliteConfigRepositoryTest, UpdatePrompts) {
    {
        SqliteConfigRepository repo(TEST_DB_PATH);

        std::vector<PromptConfig> new_prompts;
        // ID 0 indicates new insertion
        new_prompts.emplace_back(0, "test_prompt", "You are a test bot", true);

        repo.handleUpdatePrompt(new_prompts);

        auto prompts = repo.getAllPrompts();
        ASSERT_EQ(prompts.size(), 1);
        EXPECT_EQ(prompts[0].name, "test_prompt");
        EXPECT_GT(prompts[0].id, 0); // ID should be assigned
    }

    // Reload and Update
    {
        SqliteConfigRepository repo(TEST_DB_PATH);
        auto prompts = repo.getAllPrompts();
        ASSERT_EQ(prompts.size(), 1);

        // Modify existing
        prompts[0].content = "Modified content";
        // Add another
        prompts.emplace_back(0, "second_prompt", "Another prompt", false);

        repo.handleUpdatePrompt(prompts);

        auto updated_prompts = repo.getAllPrompts();
        ASSERT_EQ(updated_prompts.size(), 2);

        // Order isn't guaranteed by map/vector logic per se, but usually insertion order or ID.
        // Let's find by name
        bool found_mod = false;
        bool found_new = false;
        for(const auto& p : updated_prompts) {
            if(p.name == "test_prompt") {
                EXPECT_EQ(p.content, "Modified content");
                found_mod = true;
            }
            if(p.name == "second_prompt") {
                EXPECT_FALSE(p.is_active);
                found_new = true;
            }
        }
        EXPECT_TRUE(found_mod);
        EXPECT_TRUE(found_new);
    }
}

TEST_F(SqliteConfigRepositoryTest, UpdateChannels) {
    {
        SqliteConfigRepository repo(TEST_DB_PATH);
        std::vector<AlertChannel> channels;
        // id, name, provider, webhook, threshold, template, active
        channels.emplace_back(0, "dev_ops", "slack", "http://slack", "high", "Error: {{msg}}", true);

        repo.handleUpdateChannel(channels);

        auto result = repo.getAllChannels();
        ASSERT_EQ(result.size(), 1);
        EXPECT_EQ(result[0].name, "dev_ops");
        EXPECT_EQ(result[0].msg_template, "Error: {{msg}}");
    }

    // Test Deletion (by omitting from the list)
    {
        SqliteConfigRepository repo(TEST_DB_PATH);
        // Pass empty list -> should delete everything
        repo.handleUpdateChannel({});

        auto result = repo.getAllChannels();
        EXPECT_TRUE(result.empty());
    }
}
