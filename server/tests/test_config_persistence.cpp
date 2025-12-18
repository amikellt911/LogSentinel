#include <gtest/gtest.h>
#include "persistence/SqliteConfigRepository.h"
#include <filesystem>
#include <thread>
#include <chrono>

class ConfigPersistenceTest : public ::testing::Test {
protected:
    std::string db_path = "test_config_persistence.db";

    void SetUp() override {
        // Clean up before test
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove(db_path);
        }
    }

    void TearDown() override {
        // Clean up after test
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove(db_path);
        }
    }
};

TEST_F(ConfigPersistenceTest, SaveAndLoadNewFields) {
    {
        // 1. Initialize repository and save new values
        SqliteConfigRepository repo(db_path);

        // Retrieve current config (defaults)
        auto config = repo.getAppConfig();

        // Update with new values
        std::map<std::string, std::string> updates;
        updates["log_retention_days"] = "30";
        updates["max_disk_usage_gb"] = "10";
        updates["http_port"] = "9090";
        updates["ai_auto_degrade"] = "1";
        updates["ai_fallback_model"] = "offline-model";
        updates["ai_circuit_breaker"] = "0";
        updates["ai_failure_threshold"] = "10";
        updates["ai_cooldown_seconds"] = "120";
        updates["active_prompt_id"] = "42";

        repo.handleUpdateAppConfig(updates);
    }

    {
        // 2. Re-initialize repository and verify values
        SqliteConfigRepository repo(db_path);
        auto config = repo.getAppConfig();

        EXPECT_EQ(config.log_retention_days, 30);
        EXPECT_EQ(config.max_disk_usage_gb, 10);
        EXPECT_EQ(config.http_port, 9090);
        EXPECT_EQ(config.ai_auto_degrade, true);
        EXPECT_EQ(config.ai_fallback_model, "offline-model");
        EXPECT_EQ(config.ai_circuit_breaker, false);
        EXPECT_EQ(config.ai_failure_threshold, 10);
        EXPECT_EQ(config.ai_cooldown_seconds, 120);
        EXPECT_EQ(config.active_prompt_id, 42);
    }
}
