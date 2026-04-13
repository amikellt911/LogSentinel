#include "handlers/FrontendAssetHandler.h"

#include <fstream>
#include <sstream>

namespace
{
std::unordered_map<std::string, std::string> BuildMimeTypes()
{
    // 先只保留当前单入口部署真正会打到的最小 MIME 集合；
    // 后面如果 dist 里新增新格式资源，再按需要往这里补，不先做成一大坨“可能有用”的枚举表。
    return {
        {".html", "text/html; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".ico", "image/x-icon"},
        {".woff2", "font/woff2"},
    };
}
} // namespace

FrontendAssetHandler::FrontendAssetHandler(std::string dist_root,
                                           std::unordered_set<std::string> spa_routes)
    : dist_root_(std::filesystem::absolute(std::filesystem::path(std::move(dist_root)))),
      spa_routes_(std::move(spa_routes)),
      mime_types_(BuildMimeTypes())
{
}

bool FrontendAssetHandler::Handle(const HttpRequest& request, HttpResponse* response) const
{
    if (response == nullptr) {
        return false;
    }
    if (request.method_ != "GET") {
        return false;
    }
    if (dist_root_.empty()) {
        return false;
    }

    const std::string request_path = StripQueryAndFragment(request.path_);
    if (request_path.empty() || request_path.front() != '/') {
        return false;
    }

    // 真实静态资源必须优先命中：
    // 既然 dist 目录里已经有文件，就应该直接把文件吐给浏览器，
    // 不能因为某个路径“看起来像页面”就错误 fallback 到 index.html。
    if (request_path.size() > 1) {
        const std::filesystem::path candidate = dist_root_ / request_path.substr(1);
        if (TryServeFile(candidate, response)) {
            return true;
        }
    }

    // SPA fallback 只对白名单页面开放：
    // 这样 `/settings` 这类正式前端路由能正常刷新，
    // 但 `/fdasxz` 这种乱输路径不会被伪装成“有页面”，而是回到上层统一 404。
    if (spa_routes_.find(request_path) == spa_routes_.end()) {
        return false;
    }

    return TryServeFile(dist_root_ / "index.html", response);
}

std::string FrontendAssetHandler::StripQueryAndFragment(const std::string& raw_path)
{
    const size_t query_pos = raw_path.find_first_of("?#");
    if (query_pos == std::string::npos) {
        return raw_path;
    }
    return raw_path.substr(0, query_pos);
}

std::string FrontendAssetHandler::DetectMimeType(const std::filesystem::path& file_path) const
{
    const std::string extension = file_path.extension().string();
    const auto it = mime_types_.find(extension);
    if (it == mime_types_.end()) {
        return "application/octet-stream";
    }
    return it->second;
}

bool FrontendAssetHandler::IsPathInsideDistRoot(const std::filesystem::path& file_path) const
{
    std::error_code candidate_ec;
    const std::filesystem::path normalized_root = std::filesystem::weakly_canonical(dist_root_, candidate_ec);
    if (candidate_ec) {
        return false;
    }

    std::error_code file_ec;
    const std::filesystem::path normalized_file = std::filesystem::weakly_canonical(file_path, file_ec);
    if (file_ec) {
        return false;
    }

    auto root_it = normalized_root.begin();
    auto file_it = normalized_file.begin();
    for (; root_it != normalized_root.end() && file_it != normalized_file.end(); ++root_it, ++file_it) {
        if (*root_it != *file_it) {
            return false;
        }
    }
    return root_it == normalized_root.end();
}

bool FrontendAssetHandler::TryServeFile(const std::filesystem::path& file_path, HttpResponse* response) const
{
    if (!IsPathInsideDistRoot(file_path)) {
        return false;
    }

    std::error_code exists_ec;
    if (!std::filesystem::exists(file_path, exists_ec) ||
        !std::filesystem::is_regular_file(file_path, exists_ec)) {
        return false;
    }

    std::ifstream input(file_path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }

    std::ostringstream stream;
    stream << input.rdbuf();

    response->setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    response->setHeader("Content-Type", DetectMimeType(file_path));
    response->setBody(stream.str());
    return true;
}
