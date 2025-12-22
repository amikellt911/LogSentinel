#include "http/HttpServer.h"
#include "MiniMuduo/net/EventLoop.h"
#include "http/HttpResponse.h"
#include "threadpool/ThreadPool.h"
#include "core/AnalysisTask.h"
#include "persistence/SqliteLogRepository.h"
#include "ai/AiProvider.h"  // 引入AI抽象接口
#include "ai/GeminiApiAi.h"
#include "ai/MockAI.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <memory> // For std::unique_ptr
#include "notification/WebhookNotifier.h"
#include "persistence/SqliteConfigRepository.h"
#include "http/Router.h"
#include "handlers/LogHandler.h"
#include "handlers/DashboardHandler.h"
#include "handlers/HistoryHandler.h"
#include "handlers/ConfigHandler.h"
#include "core/LogBatcher.h"
class testServer : public HttpServer
{
public:
    testServer(MiniMuduo::net::EventLoop *loop,
               const MiniMuduo::net::InetAddress &listenAddr,
               const int num_io_thread)
        : HttpServer(loop, listenAddr, "testServer")
    {
        setThreadNum(num_io_thread);
    }

private:
};

int main(int argc, char* argv[])
{
    std::string db_path = "LogSentinel.db"; // 生产环境默认名
    int port = 8080;
    //简单的命令行参数解析
    // 支持格式: ./LogSentinel --db <path> --port <port>
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
    }
    std::shared_ptr<SqliteLogRepository> persistence;
    try
    {
        persistence = std::make_shared<SqliteLogRepository>(db_path);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal Error:Failed to initialize persistence layer" << e.what() << '\n';
        return -1;
    }

    // 创建AI客户端实例
    // std::shared_ptr<AiProvider> ai_client = std::make_shared<MockAI>();
    // TODO: 根据配置切换 AI Provider，目前先用 GeminiApiAi，因为它对接了 Proxy
    // 我们的 LogHandler 会把 config 传给 Batcher，Batcher 传给 AiProvider
    // 所以 AiProvider 本身可以是无状态的（除了 URL 配置），或者初始化一次即可
    std::shared_ptr<AiProvider> ai_client = std::make_shared<GeminiApiAi>();

    const int num_cpu_cores = std::thread::hardware_concurrency();
    const int num_io_threads = 1; // 明确 I/O 线程数量
    const int num_worker_threads = num_cpu_cores > 1 ? num_cpu_cores - num_io_threads : 1;

    std::cout << "System Info: " << num_cpu_cores << " cores detected." << std::endl;
    std::cout << "Thread Model: " << num_io_threads << " I/O threads, "
              << num_worker_threads << " worker threads." << std::endl;
    MiniMuduo::net::EventLoop loop;
    MiniMuduo::net::InetAddress addr(port);
    testServer server(&loop, addr, num_io_threads);
    ThreadPool tpool(num_worker_threads);
    auto config_repo = std::make_shared<SqliteConfigRepository>(db_path);
    //std::vector<std::string> urls = config_repo->getActiveWebhookUrls();
    std::vector<std::string> urls;
    std::shared_ptr<INotifier> notifier = std::make_shared<WebhookNotifier>(urls);
    
    std::shared_ptr<Router> router = std::make_shared<Router>();

    // Config Handler
    auto config_handler = std::make_shared<ConfigHandler>(config_repo, &tpool);
    router->add("GET", "/settings/all", [config_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        config_handler->handleGetSettings(req, resp, conn);
    });
    router->add("POST", "/settings/config", [config_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        config_handler->handleUpdateAppConfig(req, resp, conn);
    });
    router->add("POST", "/settings/prompts", [config_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        config_handler->handleUpdatePrompts(req, resp, conn);
    });
    router->add("POST", "/settings/channels", [config_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        config_handler->handleUpdateChannels(req, resp, conn);
    });
    std::shared_ptr<LogBatcher> batcher=std::make_shared<LogBatcher>(&loop,&tpool,persistence,ai_client,notifier, config_repo);
    std::shared_ptr<HistoryHandler> history_handler=std::make_shared<HistoryHandler>(persistence,&tpool);
    //lambda默认值拷贝是const,但是handlePost是非const成员函数，会导致const值变化
    //所以需要加上mutable或shared_ptr,因为他是指针，在const中，让他不会改变指向，但是可以改变值
    //LogHandler handler(&tpool,persistence,ai_client, notifier);
    // 注入 config_repo
    auto handler = std::make_shared<LogHandler>(&tpool,persistence,batcher);

    router->add("POST", "/logs", [handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        handler->handlePost(req, resp, conn);
    });
    router->add("GET", "/results/*", [handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        handler->handleGetResult(req, resp, conn);
    });
    auto dashboard_handler = std::make_shared<DashboardHandler>(persistence,&tpool);
    router->add("GET", "/dashboard", [dashboard_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        dashboard_handler->handleGetStats(req, resp, conn);
    });
    router->add("GET","/history*",[history_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn){
        history_handler->handleGetHistory(req,resp,conn);
    });
    auto onRequest=[router](const HttpRequest& req, HttpResponse* resp,const MiniMuduo::net::TcpConnectionPtr& conn){
        bool isSuccess=router->dispatch(req,resp,conn);
        if(!isSuccess)
        {
            resp->setStatusCode(HttpResponse::HttpStatusCode::k404NotFound);
            resp->addCorsHeaders();
            resp->body_ = "{\"error\": \"404 Not Found\", \"path\": \"" + req.path() + "\"}";
        }
    };
    server.setHttpCallback(onRequest);
    server.start();
    loop.loop();
    return 0;
}
