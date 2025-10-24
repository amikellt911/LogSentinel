#include<gtest/gtest.h>
#include<http/HttpContext.h>

class HttpContextTest : public ::testing::Test
{ 
    protected:
        void SetUp() override
        {
            context_=std::make_unique<HttpContext>();
        }
        std::unique_ptr<HttpContext> context_;
        MiniMuduo::net::Buffer buffer_;
};

/*
    A. 正常流程 (Happy Path)

    ParseSimpleGet: 测试一个最简单的 GET 请求，带几个 header。

    ParseSimplePost: 测试一个 POST 请求，带 Content-Length 和 Body。

    ParseRequestNoHeaders: 测试一个只有请求行和空行的 GET 请求。

    ParseRequestWithManyHeaders: 测试一个有大量 header 的请求。
*/


TEST_F(HttpContextTest, ParseSimplePost) {
    buffer_.append("POST /test HTTP/1.1\r\nHost: example.com\r\nContent-Length: 4\r\n\r\nbody");

    // 使用 ASSERT 检查关键前置条件
    ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kSuccess);
    ASSERT_TRUE(context_->gotAll());

    // 使用 EXPECT 检查所有独立属性
    const auto& req = context_->request();
    EXPECT_EQ(req.method_, "POST");
    EXPECT_EQ(req.path_, "/test");
    EXPECT_EQ(req.version_, "1.1");
    EXPECT_EQ(req.getHeader("Host"), "example.com");
    EXPECT_EQ(req.getHeader("Content-Length"), "4");
    EXPECT_EQ(req.body_, "body");
    EXPECT_EQ(buffer_.readableBytes(), 0);
}

TEST_F(HttpContextTest, ParseSimpleGet) {
    buffer_.append("GET /test HTTP/1.1\r\nHost: example.com\r\n\r\n");
    ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kSuccess);
    ASSERT_TRUE(context_->gotAll());
    const auto& req = context_->request();
    EXPECT_EQ(req.method_, "GET");
    EXPECT_EQ(req.path_, "/test");
    EXPECT_EQ(req.version_, "1.1");
    EXPECT_EQ(req.getHeader("Host"), "example.com");
}

TEST_F(HttpContextTest,ParseRequestNoHeaders){
    buffer_.append("GET /test HTTP/1.1\r\n\r\n");
    ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kSuccess);
    ASSERT_TRUE(context_->gotAll());
    const auto& req = context_->request();
    EXPECT_EQ(req.method_, "GET");
    EXPECT_EQ(req.path_, "/test");
    EXPECT_EQ(req.version_, "1.1");
    EXPECT_EQ(buffer_.readableBytes(), 0);
}

TEST_F(HttpContextTest,ParseRequestWithManyHeaders){
    buffer_.append("GET /test HTTP/1.1\r\nHost: example.com\r\nContent-Length: 4\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:143.0) Gecko/20100101 Firefox/143.0\r\nAccept: image/avif,image/webp,image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5\r\n\r\n");
    ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kSuccess);
    ASSERT_TRUE(context_->gotAll());
    const auto& req = context_->request();
    EXPECT_EQ(req.method_, "GET");
    EXPECT_EQ(req.path_, "/test");
    EXPECT_EQ(req.version_, "1.1");
    EXPECT_EQ(req.getHeader("Host"), "example.com");
    EXPECT_EQ(req.getHeader("Content-Length"), "4");
    EXPECT_EQ(req.getHeader("User-Agent"), "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:143.0) Gecko/20100101 Firefox/143.0");
    EXPECT_EQ(req.getHeader("Accept"), "image/avif,image/webp,image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5");
}

/*
    B. 边界与异常情况 (Edge & Error Cases)

    ParseMalformedRequestLine: 测试格式错误的请求行（比如少了个空格，HTTP 版本号不对）。预期 parseRequest 返回 kError。

    ParseMalformedHeader: 测试格式错误的 header（比如没有冒号）。预期返回 kError。

    ParsePostWithoutContentLength: 测试一个 POST 请求但没有 Content-Length header。预期返回 kError。

    ParseRequestWithExtraSpaces: 测试请求行和 header 里有多余的、不规范的空格。预期你的解析器能健壮地处理。
*/

TEST_F(HttpContextTest, ParseMalformedRequestLine) {
    buffer_.append("GET /testHTTP/1.1\r\nHost: example.com\r\n\r\n");
    ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kError);
}

TEST_F(HttpContextTest, ParseMalformedHeader) {
    buffer_.append("GET /test HTTP/1.1\r\nHost example.com\r\n\r\n");
    ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kError);
}
TEST_F(HttpContextTest,ParsePostWithoutContentLength){
    buffer_.append("POST /api/v1 HTTP/1.1\r\nHost: example.com\r\n\r\nbody");
    ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kError);
}

TEST_F(HttpContextTest,ParseRequestWithExtraSpaces){
    buffer_.append("GET  /test  HTTP/1.1\r\nHost:  example.com \r\n\r\n");
    ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kSuccess);
    const auto& req = context_->request();
    EXPECT_EQ(req.method_, "GET");
    EXPECT_EQ(req.path_, "/test");
    EXPECT_EQ(req.version_, "1.1");
    EXPECT_EQ(req.getHeader("Host"), "example.com");
}

/*
C. 网络行为模拟 (Network Simulation)

    ParseHalfPacketThenComplete (半包):

        buffer.append("GET /path HT") -> 调用 parseRequest -> 断言返回 kNeedMoreData。

        buffer.append("TP/1.1\r\n\r\n") -> 再次调用 parseRequest -> 断言返回 kSuccess 且内容正确。

    ParseStickyPacket (粘包):

        buffer.append(完整的请求A + 完整的请求B)。

        调用 parseRequest -> 断言返回 kSuccess。

        断言解析出的内容是请求 A。

        断言 buffer 里还剩下请求 B 的数据。

        context.reset() -> 再次调用 parseRequest -> 断言返回 kSuccess 且内容是请求 B。
        */

       TEST_F(HttpContextTest, ParseHalfPacketThenComplete) { 
            buffer_.append("GET /path HT");
            ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kNeedMoreData);
            buffer_.append("TP/1.1\r\nHost : example.com\r\n\r\n");
            ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kSuccess);
            const auto& req = context_->request();
            EXPECT_EQ(req.method_, "GET");
            EXPECT_EQ(req.path_, "/path");
            EXPECT_EQ(req.version_, "1.1");
            EXPECT_EQ(req.getHeader("Host"), "example.com");
       }

       TEST_F(HttpContextTest, ParseStickyPacket) {
            buffer_.append("GET /path HTTP/1.1\r\n\r\n");
            buffer_.append("POST /api/v1 HTTP/1.1\r\nContent-Length: 4\r\n\r\nbody");
            ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kSuccess);
            const auto& req = context_->request();
            EXPECT_EQ(req.method_, "GET");
            EXPECT_EQ(req.path_, "/path");
            EXPECT_EQ(req.version_, "1.1");
            context_->reset();
            ASSERT_EQ(context_->parseRequest(&buffer_), HttpContext::ParseResult::kSuccess);
            EXPECT_EQ(req.method_, "POST");
            EXPECT_EQ(req.path_, "/api/v1");
            EXPECT_EQ(req.version_, "1.1");
            EXPECT_EQ(req.getHeader("Content-Length"), "4");
            EXPECT_EQ(req.body_, "body");
            EXPECT_EQ(buffer_.readableBytes(), 0);
       }