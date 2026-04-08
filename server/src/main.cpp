#include "http/HttpServer.h"
#include "MiniMuduo/net/EventLoop.h"
#include "http/HttpResponse.h"
#include "threadpool/ThreadPool.h"
#include "core/AnalysisTask.h"
#include "persistence/SqliteLogRepository.h"
#include "ai/AiProvider.h"  // 引入AI抽象接口
#include "ai/GeminiApiAi.h"
#include "ai/MockAI.h"
#include "ai/TraceAiBackend.h"
#include "ai/TraceAiFactory.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <memory> // For std::unique_ptr
#include "notification/WebhookNotifier.h"
#include "persistence/BufferedTraceRepository.h"
#include "persistence/SqliteConfigRepository.h"
#include "persistence/SqliteTraceRepository.h"
#include "http/Router.h"
#include "handlers/LogHandler.h"
#include "handlers/TraceQueryHandler.h"
#include "handlers/DashboardHandler.h"
#include "handlers/HistoryHandler.h"
#include "handlers/ServiceMonitorHandler.h"
#include "handlers/ConfigHandler.h"
#include "core/LogBatcher.h"
#include "core/ServiceRuntimeAccumulator.h"
#include "core/SystemRuntimeAccumulator.h"
#include "core/TraceSessionManager.h"
#include "util/DevSubprocessManager.h"
#include <filesystem>
#include <chrono>
#include <csignal>
#include <optional>
#include <vector>

namespace {
volatile std::sig_atomic_t g_shutdown_requested = 0;

void HandleProcessSignal(int signal_number)
{
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        g_shutdown_requested = 1;
    }
}

std::optional<std::string> ResolveScriptPath(const std::vector<std::string>& candidates)
{
    for (const auto& candidate : candidates) {
        const std::filesystem::path path(candidate);
        if (std::filesystem::exists(path)) {
            // 转成绝对路径是为了避免主进程后续工作目录变化导致子进程路径失效。
            return std::filesystem::absolute(path).string();
        }
    }
    return std::nullopt;
}

std::string ToLowerCopy(std::string value)
{
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

std::string NormalizeWebhookThreshold(std::string threshold)
{
    if (threshold.empty()) {
        return "critical";
    }
    return ToLowerCopy(std::move(threshold));
}

std::vector<WebhookChannel> BuildWebhookChannelsFromSettings(const std::vector<AlertChannel>& alert_channels)
{
    std::vector<WebhookChannel> channels;
    channels.reserve(alert_channels.size());
    for (const auto& alert_channel : alert_channels) {
        // Settings 里的渠道现在先按冷启动配置消费：
        // 这里只把真正启用的渠道投影成通知层对象，避免主程序继续完全忽略 settings.channels。
        if (!alert_channel.is_active || alert_channel.webhook_url.empty()) {
            continue;
        }

        WebhookChannel channel;
        channel.provider = alert_channel.provider.empty() ? "feishu" : alert_channel.provider;
        channel.webhook_url = alert_channel.webhook_url;
        channel.enabled = true;
        channel.secret = alert_channel.secret;
        channel.threshold = NormalizeWebhookThreshold(alert_channel.alert_threshold);
        channels.push_back(std::move(channel));
    }
    return channels;
}
} // namespace

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
    bool port_explicit = false;
    // 当前处于 TraceExplorer 联调阶段，开发时默认自动拉起本地 AI proxy，
    // 这样直接点运行就能看到 trace_analysis，不需要每次手敲 --auto-start-proxy。
    // 后续如果要切回更保守的默认行为，再按阶段调整。
    bool auto_start_proxy = true;
    bool auto_start_webhook_mock = false;
    std::string trace_ai_provider = "mock";
    std::string trace_ai_base_url = "http://127.0.0.1:8001";
    int trace_ai_timeout_ms = 10000;
    int trace_sweep_interval_ms = 500;
    bool trace_sweep_interval_explicit = false;
    int trace_idle_timeout_ms = 5000;
    bool trace_idle_timeout_explicit = false;
    int worker_threads_override = -1;
    int worker_queue_size = 10000;
    int trace_capacity = 100;
    bool trace_capacity_explicit = false;
    int trace_token_limit = 0;
    bool trace_token_limit_explicit = false;
    int trace_max_dispatch_per_tick = 64;
    int trace_buffered_span_limit = 4096;
    int trace_active_session_limit = 1024;
    int service_monitor_window_minutes = 30;
    int service_monitor_bucket_seconds = 3;
    std::string webhook_provider;
    std::string webhook_url;
    std::string webhook_secret;
    bool trace_ai_provider_explicit = false;
    //简单的命令行参数解析
    // 支持格式: ./LogSentinel --db <path> --port <port> [--auto-start-deps]
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
            port_explicit = true;
        } else if (arg == "--auto-start-deps") {
            auto_start_proxy = true;
            auto_start_webhook_mock = true;
        } else if (arg == "--auto-start-proxy") {
            auto_start_proxy = true;
        } else if (arg == "--no-auto-start-proxy") {
            // 本地联调时默认自动拉起 proxy 很省事；但在某些受限环境里，
            // 你可能更想手动先起一份 proxy，再让后端直接复用，这里给一个显式关闭开关。
            auto_start_proxy = false;
        } else if (arg == "--auto-start-webhook-mock") {
            auto_start_webhook_mock = true;
        } else if (arg == "--trace-ai-provider" && i + 1 < argc) {
            trace_ai_provider = argv[++i];
            trace_ai_provider_explicit = true;
        } else if (arg == "--trace-ai-base-url" && i + 1 < argc) {
            trace_ai_base_url = argv[++i];
        } else if (arg == "--trace-ai-timeout-ms" && i + 1 < argc) {
            trace_ai_timeout_ms = std::stoi(argv[++i]);
        } else if (arg == "--trace-sweep-interval-ms" && i + 1 < argc) {
            trace_sweep_interval_ms = std::stoi(argv[++i]);
            trace_sweep_interval_explicit = true;
        } else if (arg == "--trace-idle-timeout-ms" && i + 1 < argc) {
            trace_idle_timeout_ms = std::stoi(argv[++i]);
            trace_idle_timeout_explicit = true;
        } else if (arg == "--worker-threads" && i + 1 < argc) {
            worker_threads_override = std::stoi(argv[++i]);
        } else if (arg == "--worker-queue-size" && i + 1 < argc) {
            worker_queue_size = std::stoi(argv[++i]);
        } else if (arg == "--trace-capacity" && i + 1 < argc) {
            trace_capacity = std::stoi(argv[++i]);
            trace_capacity_explicit = true;
        } else if (arg == "--trace-token-limit" && i + 1 < argc) {
            trace_token_limit = std::stoi(argv[++i]);
            trace_token_limit_explicit = true;
        } else if (arg == "--trace-max-dispatch-per-tick" && i + 1 < argc) {
            trace_max_dispatch_per_tick = std::stoi(argv[++i]);
        } else if (arg == "--trace-buffered-span-limit" && i + 1 < argc) {
            trace_buffered_span_limit = std::stoi(argv[++i]);
        } else if (arg == "--trace-active-session-limit" && i + 1 < argc) {
            trace_active_session_limit = std::stoi(argv[++i]);
        } else if (arg == "--service-monitor-window-minutes" && i + 1 < argc) {
            // 服务监控默认还是按 30 分钟窗口跑，但联调时可以临时压到 1~2 分钟，
            // 这样不用真等半小时，就能看到榜单进窗和退窗的完整过程。
            service_monitor_window_minutes = std::stoi(argv[++i]);
        } else if (arg == "--service-monitor-bucket-seconds" && i + 1 < argc) {
            // 窗口总时长和桶粒度拆开后，答辩时就能继续保留“最近 30 分钟”语义，
            // 同时把内部桶压到 3 秒，避免第一次显示必须傻等整整 1 分钟。
            service_monitor_bucket_seconds = std::stoi(argv[++i]);
        } else if (arg == "--webhook-provider" && i + 1 < argc) {
            // 这两个参数是主程序阶段的临时直连入口，先让“critical trace -> 真实飞书”
            // 单独跑通；后面再把同一套 WebhookChannel 正式接回 Settings/SQLite。
            webhook_provider = argv[++i];
        } else if (arg == "--webhook-url" && i + 1 < argc) {
            webhook_url = argv[++i];
        } else if (arg == "--webhook-secret" && i + 1 < argc) {
            // secret 只在飞书签名校验开启时才需要；为空时继续走无签名 webhook。
            webhook_secret = argv[++i];
        }
    }

    if (trace_sweep_interval_ms <= 0) {
        std::cerr << "Fatal Error: --trace-sweep-interval-ms must be > 0" << std::endl;
        return -1;
    }
    if (trace_idle_timeout_ms <= 0) {
        std::cerr << "Fatal Error: --trace-idle-timeout-ms must be > 0" << std::endl;
        return -1;
    }
    if (worker_threads_override == 0 || worker_threads_override < -1) {
        std::cerr << "Fatal Error: --worker-threads must be > 0 or omitted" << std::endl;
        return -1;
    }
    if (worker_queue_size <= 0) {
        std::cerr << "Fatal Error: --worker-queue-size must be > 0" << std::endl;
        return -1;
    }
    if (trace_capacity <= 0) {
        std::cerr << "Fatal Error: --trace-capacity must be > 0" << std::endl;
        return -1;
    }
    if (trace_token_limit < 0) {
        std::cerr << "Fatal Error: --trace-token-limit must be >= 0" << std::endl;
        return -1;
    }
    if (trace_max_dispatch_per_tick <= 0) {
        std::cerr << "Fatal Error: --trace-max-dispatch-per-tick must be > 0" << std::endl;
        return -1;
    }
    if (trace_buffered_span_limit <= 0) {
        std::cerr << "Fatal Error: --trace-buffered-span-limit must be > 0" << std::endl;
        return -1;
    }
    if (trace_active_session_limit <= 0) {
        std::cerr << "Fatal Error: --trace-active-session-limit must be > 0" << std::endl;
        return -1;
    }
    if (service_monitor_window_minutes <= 0) {
        std::cerr << "Fatal Error: --service-monitor-window-minutes must be > 0" << std::endl;
        return -1;
    }
    if (service_monitor_bucket_seconds <= 0) {
        std::cerr << "Fatal Error: --service-monitor-bucket-seconds must be > 0" << std::endl;
        return -1;
    }
    if ((webhook_provider.empty() && !webhook_url.empty()) ||
        (!webhook_provider.empty() && webhook_url.empty())) {
        std::cerr << "Fatal Error: --webhook-provider and --webhook-url must be provided together"
                  << std::endl;
        return -1;
    }

    std::signal(SIGINT, HandleProcessSignal);
    std::signal(SIGTERM, HandleProcessSignal);

    std::shared_ptr<SqliteConfigRepository> config_repo;
    try
    {
        config_repo = std::make_shared<SqliteConfigRepository>(db_path);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal Error:Failed to initialize config repository" << e.what() << '\n';
        return -1;
    }
    // 这批参数已经被收口成“冷启动配置”：
    // 既然它们会直接影响监听端口、线程池大小和 TraceSessionManager 的构造参数，
    // 那么就应该在进程启动时只读一次快照，后面运行中不再反复回头查 repo。
    const auto startup_config_snapshot = config_repo->getSnapshot();
    const AppConfig& startup_app_config = startup_config_snapshot->app_config;
    // CLI 显式覆盖仍然优先于 Settings。
    // 这样开发脚本和手工调试时还能临时顶掉库里的值，避免“为了试一个参数还得先进设置页保存”。
    const int effective_port =
        port_explicit ? port
                      : (startup_app_config.http_port > 0 ? startup_app_config.http_port : port);
    const int effective_trace_sweep_interval_ms =
        trace_sweep_interval_explicit
            ? trace_sweep_interval_ms
            : (startup_app_config.sweep_tick_ms > 0
                   ? startup_app_config.sweep_tick_ms
                   : trace_sweep_interval_ms);
    const int effective_trace_idle_timeout_ms =
        trace_idle_timeout_explicit
            ? trace_idle_timeout_ms
            : (startup_app_config.collecting_idle_timeout_ms > 0
                   ? startup_app_config.collecting_idle_timeout_ms
                   : trace_idle_timeout_ms);
    const int effective_trace_capacity =
        trace_capacity_explicit
            ? trace_capacity
            : (startup_app_config.span_capacity > 0
                   ? startup_app_config.span_capacity
                   : trace_capacity);
    const int effective_trace_token_limit =
        trace_token_limit_explicit
            ? trace_token_limit
            : (startup_app_config.token_limit >= 0
                   ? startup_app_config.token_limit
                   : trace_token_limit);
    // 这两个时间窗当前没有保留 CLI override，先统一走 Settings 冷启动配置。
    // 原因很简单：这一刀的目标就是把“已经设计成 Settings 字段的主链参数”真正接回状态机，
    // 避免继续出现库里能存、页面能改、但 TraceSessionManager 实际上仍然硬编码的假生效。
    const int effective_sealed_grace_window_ms =
        startup_app_config.sealed_grace_window_ms > 0
            ? startup_app_config.sealed_grace_window_ms
            : 1000;
    const int effective_retry_base_delay_ms =
        startup_app_config.retry_base_delay_ms > 0
            ? startup_app_config.retry_base_delay_ms
            : 500;
    // trace_end 主字段和别名现在也归到冷启动配置：
    // 它们决定的是上报 JSON 该怎么解释，不适合在运行中随手切换；否则同一份部署前后两批请求
    // 会因为设置页刚好被改过而使用不同解析口径，排查起来只会更乱。
    const std::string effective_trace_end_field =
        startup_app_config.trace_end_field.empty()
            ? "trace_end"
            : startup_app_config.trace_end_field;
    const std::vector<std::string> effective_trace_end_aliases =
        startup_app_config.trace_end_aliases;
    // 水位阈值同样收口成冷启动配置：
    // 它们直接驱动 TraceSessionManager 的背压状态机，不适合运行中热切。
    // 这一刀只开放 overload/critical 两档，low 仍由后端内部派生回滞阈值。
    const int effective_wm_active_sessions_overload =
        startup_app_config.wm_active_sessions_overload;
    const int effective_wm_active_sessions_critical =
        startup_app_config.wm_active_sessions_critical;
    const int effective_wm_buffered_spans_overload =
        startup_app_config.wm_buffered_spans_overload;
    const int effective_wm_buffered_spans_critical =
        startup_app_config.wm_buffered_spans_critical;
    const int effective_wm_pending_tasks_overload =
        startup_app_config.wm_pending_tasks_overload;
    const int effective_wm_pending_tasks_critical =
        startup_app_config.wm_pending_tasks_critical;

    if (effective_port <= 0) {
        std::cerr << "Fatal Error: effective http_port must be > 0" << std::endl;
        return -1;
    }
    if (effective_trace_sweep_interval_ms <= 0) {
        std::cerr << "Fatal Error: effective sweep_tick_ms must be > 0" << std::endl;
        return -1;
    }
    if (effective_trace_idle_timeout_ms <= 0) {
        std::cerr << "Fatal Error: effective collecting_idle_timeout_ms must be > 0" << std::endl;
        return -1;
    }
    if (effective_trace_capacity <= 0) {
        std::cerr << "Fatal Error: effective span_capacity must be > 0" << std::endl;
        return -1;
    }
    if (effective_trace_token_limit < 0) {
        std::cerr << "Fatal Error: effective token_limit must be >= 0" << std::endl;
        return -1;
    }
    if (effective_sealed_grace_window_ms <= 0) {
        std::cerr << "Fatal Error: effective sealed_grace_window_ms must be > 0" << std::endl;
        return -1;
    }
    if (effective_retry_base_delay_ms <= 0) {
        std::cerr << "Fatal Error: effective retry_base_delay_ms must be > 0" << std::endl;
        return -1;
    }
    if (effective_wm_active_sessions_overload <= 0 || effective_wm_active_sessions_overload > 100 ||
        effective_wm_active_sessions_critical < effective_wm_active_sessions_overload ||
        effective_wm_active_sessions_critical > 100) {
        std::cerr << "Fatal Error: active session watermark percentages are invalid" << std::endl;
        return -1;
    }
    if (effective_wm_buffered_spans_overload <= 0 || effective_wm_buffered_spans_overload > 100 ||
        effective_wm_buffered_spans_critical < effective_wm_buffered_spans_overload ||
        effective_wm_buffered_spans_critical > 100) {
        std::cerr << "Fatal Error: buffered spans watermark percentages are invalid" << std::endl;
        return -1;
    }
    if (effective_wm_pending_tasks_overload <= 0 || effective_wm_pending_tasks_overload > 100 ||
        effective_wm_pending_tasks_critical < effective_wm_pending_tasks_overload ||
        effective_wm_pending_tasks_critical > 100) {
        std::cerr << "Fatal Error: pending tasks watermark percentages are invalid" << std::endl;
        return -1;
    }

    DevSubprocessManager dev_process_manager;
    if (auto_start_proxy) {
        std::optional<std::string> proxy_script = ResolveScriptPath({
            "server/ai/proxy/main.py",
            "ai/proxy/main.py",
            "../ai/proxy/main.py",
            "../../server/ai/proxy/main.py"
        });
        if (!proxy_script.has_value()) {
            std::cerr << "Fatal Error: cannot find AI proxy script (main.py)." << std::endl;
            return -1;
        }
        if (!dev_process_manager.EnsurePythonService("ai-proxy", proxy_script.value(), 8001)) {
            std::cerr << "Fatal Error: AI proxy failed to start." << std::endl;
            return -1;
        }
    }

    if (auto_start_webhook_mock) {
        std::optional<std::string> webhook_script = ResolveScriptPath({
            "server/tests/mock_webhook_server.py",
            "tests/mock_webhook_server.py",
            "../tests/mock_webhook_server.py",
            "../../server/tests/mock_webhook_server.py"
        });
        if (!webhook_script.has_value()) {
            std::cerr << "Fatal Error: cannot find mock webhook server script." << std::endl;
            return -1;
        }
        if (!dev_process_manager.EnsurePythonService("mock-webhook", webhook_script.value(), 9999)) {
            std::cerr << "Fatal Error: mock webhook server failed to start." << std::endl;
            return -1;
        }
    }

    std::shared_ptr<SqliteLogRepository> persistence;
    std::shared_ptr<SqliteTraceRepository> trace_repo;
    std::shared_ptr<SqliteTraceRepository> trace_read_repo;
    std::shared_ptr<BufferedTraceRepository> buffered_trace_repo;
    try
    {
        persistence = std::make_shared<SqliteLogRepository>(db_path);
        trace_repo = std::make_shared<SqliteTraceRepository>(db_path);
        trace_read_repo = std::make_shared<SqliteTraceRepository>(db_path);
        // Trace 主数据和分析结果现在都先走双缓冲写入器，再由后台 flush 线程批量落到 SQLite。
        buffered_trace_repo = std::make_shared<BufferedTraceRepository>(trace_repo);
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
    const int default_worker_threads = num_cpu_cores > 1 ? num_cpu_cores - num_io_threads : 1;
    // worker 线程数和端口一样属于冷启动参数：
    // 既然线程池创建后不会在运行中自动扩缩，那么这里就只在启动时做一次“CLI > Settings > 默认值”的决策。
    const int num_worker_threads =
        worker_threads_override > 0
            ? worker_threads_override
            : (startup_app_config.kernel_worker_threads > 0
                   ? startup_app_config.kernel_worker_threads
                   : default_worker_threads);
    const int num_query_threads = 1;

    std::cout << "System Info: " << num_cpu_cores << " cores detected." << std::endl;
    std::cout << "Thread Model: " << num_io_threads << " I/O threads, "
              << num_worker_threads << " worker threads, "
              << num_query_threads << " query threads." << std::endl;
    MiniMuduo::net::EventLoop loop;
    MiniMuduo::net::InetAddress addr(effective_port);
    testServer server(&loop, addr, num_io_threads);
    std::vector<WebhookChannel> webhook_channels =
        BuildWebhookChannelsFromSettings(startup_config_snapshot->channels);
    if (auto_start_webhook_mock) {
        // 开发时自动注入本地 mock webhook，避免“服务已经拉起，但压根没有通知目标”的调试盲区。
        // 这里显式按 generic 渠道注入，是为了让 mock webhook 和真实飞书 webhook 可以并存，而不是互相覆盖。
        webhook_channels.push_back(WebhookChannel{"generic", "http://127.0.0.1:9999/webhook", true, "", "critical"});
    }
    if (!webhook_url.empty()) {
        // CLI 直连入口现在降级成调试兜底：
        // settings.channels 已经会在启动时进入 notifier，这里只额外补一个手工 override，
        // 保住现有脚本和答辩临时联调入口，不要求每次都先去页面里保存。
        webhook_channels.push_back(WebhookChannel{webhook_provider, webhook_url, true, webhook_secret, "critical"});
    }
    if (!webhook_channels.empty()) {
        std::cout << "Webhook notifier enabled. channels=" << webhook_channels.size() << std::endl;
        for (const auto& channel : webhook_channels) {
            std::cout << "  - provider=" << channel.provider
                      << ", url=" << channel.webhook_url
                      << ", threshold=" << channel.threshold
                      << ", enabled=" << (channel.enabled ? "true" : "false")
                      << std::endl;
        }
    }
    std::shared_ptr<INotifier> notifier = std::make_shared<WebhookNotifier>(std::move(webhook_channels));
    std::shared_ptr<TraceAiProvider> trace_ai;
    const bool enable_trace_ai = auto_start_proxy || trace_ai_provider_explicit;
    if (enable_trace_ai) {
        TraceAiBackend backend = TraceAiBackend::Mock;
        if (!TryParseTraceAiBackend(trace_ai_provider, &backend)) {
            std::cerr << "Fatal Error: unsupported --trace-ai-provider '"
                      << trace_ai_provider << "'. expected one of: mock|gemini" << std::endl;
            return -1;
        }
        TraceAiFactoryOptions options;
        options.base_url = trace_ai_base_url;
        options.backend = backend;
        options.timeout_ms = trace_ai_timeout_ms;
        trace_ai = CreateTraceAiProvider(options);
        std::cout << "Trace AI enabled via proxy. provider=" << trace_ai_provider
                  << ", base_url=" << trace_ai_base_url
                  << ", timeout_ms=" << trace_ai_timeout_ms << std::endl;
    }
    // 线程池需要在 trace_ai/notifier 之前回收：
    // 既然 worker 任务里拿的是这些对象的裸指针，那么退出时必须先 join worker，
    // 再销毁依赖对象，否则就会在“任务还在跑、对象先析构”时踩悬空指针。
    ThreadPool tpool(num_worker_threads, static_cast<size_t>(worker_queue_size));
    // Trace 读请求单独走查询线程池，避免前端查库任务和 AI/聚合任务抢同一条队列。
    // 当前先固定 1 条查询线程，把“执行通道分离”先做出来，后面再按压测结果调整线程数。
    ThreadPool query_tpool(static_cast<size_t>(num_query_threads), static_cast<size_t>(worker_queue_size));
    auto system_runtime_accumulator = std::make_shared<SystemRuntimeAccumulator>();
    auto service_runtime_accumulator = std::make_shared<ServiceRuntimeAccumulator>(/*service_top_k*/4,
                                                                                  /*operation_top_k*/6,
                                                                                  /*recent_sample_limit*/3,
                                                                                  static_cast<size_t>(service_monitor_window_minutes),
                                                                                  static_cast<size_t>(service_monitor_bucket_seconds));
    std::shared_ptr<TraceSessionManager> trace_session_manager = std::make_shared<TraceSessionManager>(
        &tpool,
        buffered_trace_repo.get(),
        trace_ai.get(),
        /*capacity*/static_cast<size_t>(effective_trace_capacity),
        /*token_limit*/static_cast<size_t>(effective_trace_token_limit),
        notifier.get(),
        effective_trace_idle_timeout_ms,
        effective_trace_sweep_interval_ms,
        effective_sealed_grace_window_ms,
        effective_retry_base_delay_ms,
        /*wheel_size*/512,
        static_cast<size_t>(trace_buffered_span_limit),
        static_cast<size_t>(trace_active_session_limit),
        effective_wm_active_sessions_overload,
        effective_wm_active_sessions_critical,
        effective_wm_buffered_spans_overload,
        effective_wm_buffered_spans_critical,
        effective_wm_pending_tasks_overload,
        effective_wm_pending_tasks_critical,
        service_runtime_accumulator.get(),
        system_runtime_accumulator.get());
    const double trace_sweep_interval_sec =
        static_cast<double>(effective_trace_sweep_interval_ms) / 1000.0;
    std::cout << "Trace session sweep enabled. sweep_interval_ms=" << effective_trace_sweep_interval_ms
              << ", idle_timeout_ms=" << effective_trace_idle_timeout_ms
              << ", sealed_grace_window_ms=" << effective_sealed_grace_window_ms
              << ", retry_base_delay_ms=" << effective_retry_base_delay_ms
              << ", max_dispatch_per_tick=" << trace_max_dispatch_per_tick
              << ", trace_capacity=" << effective_trace_capacity
              << ", trace_token_limit=" << effective_trace_token_limit
              << ", wm_active_sessions=" << effective_wm_active_sessions_overload << "/" << effective_wm_active_sessions_critical
              << ", wm_buffered_spans=" << effective_wm_buffered_spans_overload << "/" << effective_wm_buffered_spans_critical
              << ", wm_pending_tasks=" << effective_wm_pending_tasks_overload << "/" << effective_wm_pending_tasks_critical
              << ", buffered_span_limit=" << trace_buffered_span_limit
              << ", active_session_limit=" << trace_active_session_limit
              << ", worker_queue_size=" << worker_queue_size << std::endl;
    std::cout << "Service monitor window enabled. window_minutes=" << service_monitor_window_minutes
              << ", bucket_seconds=" << service_monitor_bucket_seconds << std::endl;
    TraceSessionManager* trace_session_manager_raw = trace_session_manager.get();
    loop.runEvery(trace_sweep_interval_sec, [trace_session_manager_raw,
                                             effective_trace_idle_timeout_ms,
                                             trace_max_dispatch_per_tick]() {
        const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        trace_session_manager_raw->SweepExpiredSessions(
            now_ms,
            effective_trace_idle_timeout_ms,
            static_cast<size_t>(trace_max_dispatch_per_tick));
    });
    ServiceRuntimeAccumulator* service_runtime_accumulator_raw = service_runtime_accumulator.get();
    // 这里先把服务监控快照发布周期压到 1 秒，原因不是统计更“实时”了，
    // 而是答辩演示时不希望前端明明已经过了分钟封口点，却还要额外再等 5 秒才看到榜单变化。
    // 但是要注意：分钟桶仍然是“封口后才进窗”，所以这只能减少快照发布等待，
    // 不能消除“当前活跃分钟必须等结束后才能显示”的那部分延迟。
    loop.runEvery(1.0, [service_runtime_accumulator_raw]() {
        if (service_runtime_accumulator_raw) {
            service_runtime_accumulator_raw->OnTick();
        }
    });
    SystemRuntimeAccumulator* system_runtime_accumulator_raw = system_runtime_accumulator.get();
    // 这一步先把系统监控埋点和定时采样链接通，但还不切 /dashboard 的旧 handler。
    // 这样可以先把主链统计口径做稳，再在下一刀只替换读取入口，避免这轮范围继续扩大。
    loop.runEvery(1.0, [system_runtime_accumulator_raw]() {
        if (system_runtime_accumulator_raw) {
            system_runtime_accumulator_raw->OnTick();
        }
    });
    bool shutdown_stats_logged = false;
    loop.runEvery(0.1, [&loop, &shutdown_stats_logged, trace_session_manager_raw, buffered_trace_repo]() {
        // signal handler 里只能做极少的事情，所以这里只记录退出意图；
        // 真正的 quit 放回 EventLoop 线程执行，这样对象析构和埋点打印才会走完整。
        if (g_shutdown_requested != 0) {
            if (!shutdown_stats_logged) {
                shutdown_stats_logged = true;
                std::clog << "[TraceRuntimeStats] "
                          << trace_session_manager_raw->DescribeRuntimeStats() << std::endl;
                std::clog << "[BufferedTraceRuntimeStats] "
                          << buffered_trace_repo->DescribeRuntimeStats() << std::endl;
            }
            loop.quit();
        }
    });
    
    std::shared_ptr<Router> router = std::make_shared<Router>();
    auto trace_query_handler = std::make_shared<TraceQueryHandler>(trace_read_repo, &query_tpool);
    auto service_monitor_handler = std::make_shared<ServiceMonitorHandler>(service_runtime_accumulator);

    router->add("POST", "/traces/search", [trace_query_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        trace_query_handler->handleSearchTraces(req, resp, conn);
    });
    router->add("GET", "/traces/*", [trace_query_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        trace_query_handler->handleGetTraceDetail(req, resp, conn);
    });
    router->add("GET", "/service-monitor/runtime", [service_monitor_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        service_monitor_handler->handleGetRuntimeSnapshot(req, resp, conn);
    });

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
    // /logs/spans 的结束字段口径已经收口成冷启动配置，
    // 所以这里直接把启动期算好的主字段和别名注入给 LogHandler，不再让请求热路径回头读 repo。
    auto handler = std::make_shared<LogHandler>(&tpool,
                                                persistence,
                                                batcher,
                                                trace_session_manager.get(),
                                                system_runtime_accumulator.get(),
                                                effective_trace_end_field,
                                                effective_trace_end_aliases);

    router->add("POST", "/logs", [handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        handler->handlePost(req, resp, conn);
    });
    router->add("POST", "/logs/spans", [handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        handler->handleTracePost(req, resp, conn);
    });
    router->add("GET", "/results/*", [handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        handler->handleGetResult(req, resp, conn);
    });
    // /dashboard 这一刀正式切到 SystemRuntimeAccumulator 快照。
    // 这样系统监控页先吃到主链路埋点的真值，不再绕回 SQLite 旧 dashboard 统计。
    auto dashboard_handler = std::make_shared<DashboardHandler>(system_runtime_accumulator);
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
