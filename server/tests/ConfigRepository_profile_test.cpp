#include <gtest/gtest.h>

#include "persistence/SqliteConfigRepository.h"

TEST(ConfigRepositoryProfileTest, DefaultsContainProviderProfilesAndNoLegacyAiModelKeys)
{
    SqliteConfigRepository repo(":memory:");
    const auto settings = repo.getAllSettings();

    // profile 表是新的 model/key 唯一来源。
    // 旧 ai_model/ai_api_key 只会制造“切了 provider 但 model 还停在上一家”的错配，所以新库里不再暴露它们。
    ASSERT_EQ(settings.provider_profiles.size(), 4U);
    const auto& profiles = settings.provider_profiles;
    EXPECT_EQ(profiles.at("mock").model, "mock-trace-analyzer");
    EXPECT_EQ(profiles.at("mock").api_key, "88888888");
    EXPECT_EQ(profiles.at("gemini").model, "gemini-3-pro-preview");
    EXPECT_EQ(profiles.at("glm").model, "glm-5.1");
    EXPECT_EQ(profiles.at("deepseek").model, "deepseek-v4-flash");
}

TEST(ConfigRepositoryProfileTest, UpdatingProviderProfileBumpsTraceAiRuntimeVersion)
{
    SqliteConfigRepository repo(":memory:");
    const uint64_t before = repo.getTraceAiRuntimeVersion();

    repo.handleUpdateProviderProfiles({
        ProviderProfile{"deepseek", "deepseek-v4-pro", "deepseek-key"},
    });

    const auto snapshot = repo.getSnapshot();
    ASSERT_TRUE(snapshot);
    const auto profile = snapshot->resolveProviderProfile("deepseek");
    ASSERT_TRUE(profile.has_value());
    EXPECT_EQ(profile->model, "deepseek-v4-pro");
    EXPECT_EQ(profile->api_key, "deepseek-key");
    EXPECT_GT(repo.getTraceAiRuntimeVersion(), before);
}
