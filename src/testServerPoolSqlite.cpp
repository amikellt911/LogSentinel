#include "http/HttpServer.h"
#include "MiniMuduo/net/EventLoop.h"
#include "http/HttpResponse.h"
#include "threadpool/ThreadPool.h"
#include "core/AnalysisTask.h"
#include "persistence/SqlitePersistence.h"
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
    std::shared_ptr<SqlitePersistence> persistence;
    try
    {
        persistence=std::make_shared<SqlitePersistence>("test.db");
    }
    catch(const std::exception& e)
    {
        std::cerr <<"Fatal Error:Failed to initialize persistence layer"<< e.what() << '\n';
        return -1;
    }
    
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
    //使用值捕获persistence，增加引用计数防止意外发生
    auto onRequest = [&tpool,persistence](HttpRequest &req, HttpResponse *resp)
    {
        AnalysisTask task;
        task.trace_id = std::move(req.trace_id);
        task.raw_request_body = std::move(req.body_);
        //按值捕获，意味着request返回了，task还存在，不能图那一点点速度，导致task执行时已经变成空悬引用
        auto work = [task,persistence]
        {
            try
            {
                persistence->SaveRawLog(task.trace_id, task.raw_request_body);
            }
            catch(const std::exception& e)
            {
                std::cerr <<"Work thread error: cannot save log!" <<e.what() << '\n';
                //未来可能加入重试机制
            }
            
        };
        if (tpool.submit(std::move(work)))
        {
            resp->setStatusCode(HttpResponse::HttpStatusCode::k202Acceptd);
            resp->setHeader("Content-Type", "application/json");
            resp->body_ = "{\"trace_id\": \"" + task.trace_id + "\"}";
        }
        else
        {
            resp->setStatusCode(HttpResponse::HttpStatusCode::k503ServiceUnavailable);
            resp->body_ = "{\"error\": \"Server is overloaded\"}";
        }
    };
    server.setHttpCallback(onRequest);
    server.start();
    loop.loop();
    return 0;
}
