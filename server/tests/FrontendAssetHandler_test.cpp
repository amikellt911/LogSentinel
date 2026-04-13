#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "handlers/FrontendAssetHandler.h"

namespace
{
std::filesystem::path MakeTempRoot()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("frontend-asset-handler-test-" + std::to_string(now));
}

void WriteFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    ASSERT_TRUE(output.is_open());
    output << content;
}
} // namespace

class FrontendAssetHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root_ = MakeTempRoot();
        std::filesystem::create_directories(root_);

        // 这里故意把 index.html 和 assets 文件都落成真实文件，
        // 因为我们要锁住“优先命中真实静态文件、页面白名单再 fallback”的两层语义。
        WriteFile(root_ / "index.html", "<html><body>spa-shell</body></html>");
        WriteFile(root_ / "assets" / "app.js", "console.log('frontend');");
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    FrontendAssetHandler MakeHandler() const
    {
        // 白名单只保留正式交付态页面；
        // 未列入这里的路径后面必须 404，不能一股脑全回 index.html。
        return FrontendAssetHandler(root_.string(), {"/", "/service", "/traces", "/settings"});
    }

    std::filesystem::path root_;
};

TEST_F(FrontendAssetHandlerTest, ServesExistingStaticFileWithMimeType)
{
    FrontendAssetHandler handler = MakeHandler();
    HttpRequest request;
    request.method_ = "GET";
    request.path_ = "/assets/app.js";
    HttpResponse response;

    // 这条测试锁的是“真实文件优先”：
    // 既然 dist 目录里已经存在 app.js，那么 handler 就必须直接回文件内容和 JS MIME，
    // 不能错误地把它当成 SPA 页面 fallback 到 index.html。
    const bool handled = handler.Handle(request, &response);

    ASSERT_TRUE(handled);
    EXPECT_EQ(response.statusCode_, HttpResponse::HttpStatusCode::k200Ok);
    EXPECT_EQ(response.headers_.at("Content-Type"), "application/javascript; charset=utf-8");
    EXPECT_EQ(response.body_, "console.log('frontend');");
}

TEST_F(FrontendAssetHandlerTest, FallsBackToIndexForWhitelistedSpaRoute)
{
    FrontendAssetHandler handler = MakeHandler();
    HttpRequest request;
    request.method_ = "GET";
    request.path_ = "/settings";
    HttpResponse response;

    // 这条测试锁的是“页面白名单 fallback”：
    // /settings 本身不是磁盘文件，但它是正式前端路由，所以应该回 index.html 让前端 Router 接管。
    const bool handled = handler.Handle(request, &response);

    ASSERT_TRUE(handled);
    EXPECT_EQ(response.statusCode_, HttpResponse::HttpStatusCode::k200Ok);
    EXPECT_EQ(response.headers_.at("Content-Type"), "text/html; charset=utf-8");
    EXPECT_EQ(response.body_, "<html><body>spa-shell</body></html>");
}

TEST_F(FrontendAssetHandlerTest, ReturnsNotHandledForUnknownPathOutsideWhitelist)
{
    FrontendAssetHandler handler = MakeHandler();
    HttpRequest request;
    request.method_ = "GET";
    request.path_ = "/fdasxz";
    HttpResponse response;

    // 这条测试锁的是“未知路径不能误判成 SPA 页面”：
    // 只有白名单页面才允许 fallback，其余路径要留给上层统一 404。
    const bool handled = handler.Handle(request, &response);

    EXPECT_FALSE(handled);
    EXPECT_EQ(response.statusCode_, HttpResponse::HttpStatusCode::kUnknown);
    EXPECT_TRUE(response.body_.empty());
}

TEST_F(FrontendAssetHandlerTest, RejectsPathTraversalAttempt)
{
    FrontendAssetHandler handler = MakeHandler();
    HttpRequest request;
    request.method_ = "GET";
    request.path_ = "/../secret.txt";
    HttpResponse response;

    // 这条测试锁的是路径安全：
    // 浏览器发来的路径不允许越过 dist 根目录，不然静态资源托管会变成任意文件读取入口。
    const bool handled = handler.Handle(request, &response);

    EXPECT_FALSE(handled);
    EXPECT_EQ(response.statusCode_, HttpResponse::HttpStatusCode::kUnknown);
}
