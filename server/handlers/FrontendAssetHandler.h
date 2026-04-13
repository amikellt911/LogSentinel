#pragma once

#include "http/HttpRequest.h"
#include "http/HttpResponse.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

class FrontendAssetHandler
{
public:
    // 这个 handler 只负责交付态前端资源托管：
    // 既不碰 API 分流，也不兜底所有未知路径。
    // 上层 main.cpp 只在“当前请求已经确认不是后端 API”之后，才把请求交给它尝试处理。
    FrontendAssetHandler(std::string dist_root,
                         std::unordered_set<std::string> spa_routes);

    // 返回 true 表示这个请求已经被静态资源层接管并写好了响应；
    // 返回 false 表示它既不是合法静态文件，也不是允许 fallback 的前端页面，
    // 上层应该继续走自己的 404 逻辑，而不是把这里当成异常。
    bool Handle(const HttpRequest& request, HttpResponse* response) const;

private:
    static std::string StripQueryAndFragment(const std::string& raw_path);
    std::string DetectMimeType(const std::filesystem::path& file_path) const;

    // 这里用 weakly_canonical 做根目录校验：
    // 既然浏览器路径可能携带 ".."，那么在真正读文件前必须先把相对段折叠掉，
    // 再确认最终目标仍然位于 dist_root_ 下面，避免静态文件入口被拿去探测宿主机文件。
    bool IsPathInsideDistRoot(const std::filesystem::path& file_path) const;
    bool TryServeFile(const std::filesystem::path& file_path, HttpResponse* response) const;

    std::filesystem::path dist_root_;
    std::unordered_set<std::string> spa_routes_;
    std::unordered_map<std::string, std::string> mime_types_;
};
