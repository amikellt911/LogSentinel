#include "http/HttpServer.h"
#include "MiniMuduo/net/EventLoop.h"
#include "http/HttpResponse.h"
#include "threadpool/ThreadPool.h"
#include "core/AnalysisTask.h"
#include "persistence/SqliteLogRepository.h"
#include "ai/AiProvider.h"  // 引入AI抽象接口
#include "ai/GeminiApiAi.h" // 引入Gemini AI实现
#include <MiniMuduo/net/TcpConnection.h>
#include <memory> // For std::unique_ptr
#include "notification/WebhookNotifier.h"
#include "persistence/SqliteConfigRepository.h"
#include "http/Router.h"
#include "handlers/LogHandler.h"
#include "handlers/DashboardHandler.h"
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

int main()
{
    std::shared_ptr<SqliteLogRepository> persistence;
    try
    {
        persistence = std::make_shared<SqliteLogRepository>("test.db");
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal Error:Failed to initialize persistence layer" << e.what() << '\n';
        return -1;
    }

    // 创建AI客户端实例
    std::shared_ptr<AiProvider> ai_client = std::make_shared<GeminiApiAi>();

    const int num_cpu_cores = std::thread::hardware_concurrency();
    const int num_io_threads = 1; // 明确 I/O 线程数量
    const int num_worker_threads = num_cpu_cores > 1 ? num_cpu_cores - num_io_threads : 1;

    std::cout << "System Info: " << num_cpu_cores << " cores detected." << std::endl;
    std::cout << "Thread Model: " << num_io_threads << " I/O threads, "
              << num_worker_threads << " worker threads." << std::endl;
    MiniMuduo::net::EventLoop loop;
    MiniMuduo::net::InetAddress addr(8080);
    testServer server(&loop, addr, num_io_threads);
    ThreadPool tpool(num_worker_threads);
    SqliteConfigRepository config_repo = SqliteConfigRepository("test.db");
    std::vector<std::string> urls = config_repo.getActiveWebhookUrls();
    std::shared_ptr<INotifier> notifier = std::make_shared<WebhookNotifier>(urls);
    
    std::shared_ptr<Router> router = std::make_shared<Router>();
    std::shared_ptr<LogBatcher> batcher=std::make_shared<LogBatcher>(&loop,&tpool,persistence,ai_client,notifier);
    //lambda默认值拷贝是const,但是handlePost是非const成员函数，会导致const值变化
    //所以需要加上mutable或shared_ptr,因为他是指针，在const中，让他不会改变指向，但是可以改变值
    //LogHandler handler(&tpool,persistence,ai_client, notifier);
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
