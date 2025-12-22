#pragma once
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include "persistence/ConfigTypes.h"

struct SystemConfig {
    const AppConfig app_config;
    // 分离 Map 和 Reduce 的 Prompt 列表
    const std::vector<PromptConfig> map_prompts;
    const std::vector<PromptConfig> reduce_prompts;
    const std::vector<AlertChannel> channels;

    // 预计算的 Active Prompt，实现 O(1) 访问
    const std::string active_map_prompt;
    const std::string active_reduce_prompt;

    SystemConfig(AppConfig app,
                 std::vector<PromptConfig> map_p,
                 std::vector<PromptConfig> reduce_p,
                 std::vector<AlertChannel> c)
        : app_config(std::move(app)),
          map_prompts(std::move(map_p)),
          reduce_prompts(std::move(reduce_p)),
          channels(std::move(c)),
          active_map_prompt(resolvePrompt(map_prompts, app_config.active_map_prompt_id)),
          active_reduce_prompt(resolvePrompt(reduce_prompts, app_config.active_reduce_prompt_id))
          {}

    SystemConfig& operator=(const SystemConfig&) = delete;

private:
    // 解析 Prompt 内容
    static std::string resolvePrompt(const std::vector<PromptConfig>& prompt_list, int target_id) {
        // 1. 尝试通过 ID 查找
        auto it = std::find_if(prompt_list.begin(), prompt_list.end(), [&](const PromptConfig& p) {
            return p.id == target_id;
        });

        if (it != prompt_list.end()) {
            return it->content;
        }

        // 2. 回退策略：查找第一个 Active 的 Prompt
        auto it_fallback = std::find_if(prompt_list.begin(), prompt_list.end(), [&](const PromptConfig& p) {
            return p.is_active;
        });

        if (it_fallback != prompt_list.end()) {
            return it_fallback->content;
        }

        return "";
    }
};

using SystemConfigPtr = std::shared_ptr<const SystemConfig>;
