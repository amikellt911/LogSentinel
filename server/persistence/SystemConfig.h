#pragma once
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include "persistence/ConfigTypes.h"

struct SystemConfig {
    const AppConfig app_config;
    // 单一 Prompt 列表
    const std::vector<PromptConfig> prompts;
    const std::vector<AlertChannel> channels;

    // 预计算的 Active Prompt，实现 O(1) 访问
    // 移除 const 修饰符以避免在构造函数体中使用 const_cast (虽然对象本身通过 shared_ptr<const> 访问是安全的)
    std::string active_prompt;

    // 索引缓存：通过 ID 快速查找 Content
    // 这在构建时生成，属于 "空间换时间"，避免运行时 O(N) 遍历
    std::unordered_map<int, std::string> prompt_index_;

    SystemConfig(AppConfig app,
                 std::vector<PromptConfig> prompts_,
                 std::vector<AlertChannel> c)
        : app_config(std::move(app)),
          prompts(std::move(prompts_)),
          channels(std::move(c))
    {
        // 1. 构建索引 (O(N))
        for (const auto& p : prompts) {
            prompt_index_[p.id] = p.content;
        }

        // 2. 解析 Active Prompt (O(1) 查找)
        active_prompt = resolvePromptWithIndex(prompt_index_, prompts, app_config.active_prompt_id);
    }

    SystemConfig& operator=(const SystemConfig&) = delete;

private:
    // 优化后的解析逻辑：优先查表 (O(1))，失败则找第一个 Active (O(N))
    static std::string resolvePromptWithIndex(const std::unordered_map<int, std::string>& index,
                                              const std::vector<PromptConfig>& list,
                                              int target_id)
    {
        // 1. 尝试 O(1) 查表
        auto it = index.find(target_id);
        if (it != index.end()) {
            return it->second;
        }

        // 2. 回退策略：查找第一个 Active 的 Prompt (O(N))
        // 这种情况只发生在 target_id 不存在时（比如删除了），需要 fallback
        auto it_fallback = std::find_if(list.begin(), list.end(), [&](const PromptConfig& p) {
            return p.is_active;
        });

        if (it_fallback != list.end()) {
            return it_fallback->content;
        }

        return "";
    }
};

using SystemConfigPtr = std::shared_ptr<const SystemConfig>;
