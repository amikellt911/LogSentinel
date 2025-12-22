#pragma once
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include "persistence/ConfigTypes.h"

struct SystemConfig {
    const AppConfig app_config;
    const std::vector<PromptConfig> prompts;
    const std::vector<AlertChannel> channels;

    // Pre-calculated active prompts for O(1) access
    const std::string active_map_prompt;
    const std::string active_reduce_prompt;

    SystemConfig(AppConfig app,
                 std::vector<PromptConfig> p,
                 std::vector<AlertChannel> c)
        : app_config(std::move(app)),
          prompts(std::move(p)),
          channels(std::move(c)),
          active_map_prompt(resolvePrompt(prompts, app_config.active_map_prompt_id, "map")),
          active_reduce_prompt(resolvePrompt(prompts, app_config.active_reduce_prompt_id, "reduce"))
          {}

    SystemConfig& operator=(const SystemConfig&) = delete;

private:
    static std::string resolvePrompt(const std::vector<PromptConfig>& prompts, int target_id, const std::string& type) {
        // 1. Try to find by ID and Type
        auto it = std::find_if(prompts.begin(), prompts.end(), [&](const PromptConfig& p) {
            // Check ID
            if (p.id != target_id) return false;
            // Check Type (allow empty type to match 'map' for backward compat)
            if (p.type == type) return true;
            if (type == "map" && p.type.empty()) return true;
            return false;
        });

        if (it != prompts.end()) {
            return it->content;
        }

        // 2. Fallback: Find first active prompt of that type
        auto it_fallback = std::find_if(prompts.begin(), prompts.end(), [&](const PromptConfig& p) {
            if (!p.is_active) return false;
            if (p.type == type) return true;
            if (type == "map" && p.type.empty()) return true;
            return false;
        });

        if (it_fallback != prompts.end()) {
            return it_fallback->content;
        }

        return "";
    }
};

using SystemConfigPtr = std::shared_ptr<const SystemConfig>;
