#include <gtest/gtest.h>
#include <persistence/SqliteConfigRepository.h>
#include <filesystem>
#include <fstream>

// 使用单独的数据库文件进行测试
const std::string TEST_DB_PATH = "test_config_repo.db";

class SqliteConfigRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 在每个测试前确保环境干净
        cleanup();
    }

    void TearDown() override {
        // 测试结束后清理
        cleanup();
    }

    void cleanup() {
        if (std::filesystem::exists(TEST_DB_PATH)) {
            std::filesystem::remove(TEST_DB_PATH);
        }
        // 如果存在 wal/shm 文件也一并删除
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

    // 验证默认应用配置值是否已加载（如构造函数 SQL 中定义）
    AppConfig config = repo.getAppConfig();
    EXPECT_EQ(config.ai_provider, "openai");
    EXPECT_EQ(config.kernel_worker_threads, 4);

    // 验证提示词是否为空或包含默认值（当前代码未插入默认提示词）
    auto prompts = repo.getAllPrompts();
    EXPECT_TRUE(prompts.empty());

    // 验证渠道是否为空
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

        // 立即检查缓存更新
        AppConfig config = repo.getAppConfig();
        EXPECT_EQ(config.ai_provider, "gemini");
        EXPECT_EQ(config.kernel_worker_threads, 8);
    }

    // 从数据库重新加载以检查持久性
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
        // ID 0 表示新插入
        new_prompts.emplace_back(0, "test_prompt", "You are a test bot", true);

        repo.handleUpdatePrompt(new_prompts);

        auto prompts = repo.getAllPrompts();
        ASSERT_EQ(prompts.size(), 1);
        EXPECT_EQ(prompts[0].name, "test_prompt");
        EXPECT_GT(prompts[0].id, 0); // 应该分配了 ID
    }

    // 重新加载并更新
    {
        SqliteConfigRepository repo(TEST_DB_PATH);
        auto prompts = repo.getAllPrompts();
        ASSERT_EQ(prompts.size(), 1);

        // 修改现有项
        prompts[0].content = "Modified content";
        // 添加另一项
        prompts.emplace_back(0, "second_prompt", "Another prompt", false);

        repo.handleUpdatePrompt(prompts);

        auto updated_prompts = repo.getAllPrompts();
        ASSERT_EQ(updated_prompts.size(), 2);

        // map/vector 的顺序本身不保证，通常按插入顺序或 ID。
        // 这里按名称查找验证
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

    // 测试删除（通过从列表中省略）
    {
        SqliteConfigRepository repo(TEST_DB_PATH);
        // 传入空列表 -> 应该删除所有内容
        repo.handleUpdateChannel({});

        auto result = repo.getAllChannels();
        EXPECT_TRUE(result.empty());
    }
}
