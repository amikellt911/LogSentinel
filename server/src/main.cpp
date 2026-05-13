#include "http/HttpServer.h"
#include "MiniMuduo/net/EventLoop.h"
#include "http/HttpResponse.h"
#include "threadpool/ThreadPool.h"
#include "ai/TraceAiBackend.h"
#include "ai/TraceAiFactory.h"
#include "ai/TracePromptRenderer.h"
#include <MiniMuduo/net/TcpConnection.h>
#include <memory> // For std::unique_ptr
#include "notification/WebhookNotifier.h"
#include "persistence/BufferedTraceRepository.h"
#include "persistence/DirectTraceWriteSink.h"
#include "persistence/SqliteConfigRepository.h"
#include "persistence/SqliteTraceRepository.h"
#include "persistence/TraceWriteSink.h"
#include "http/Router.h"
#include "handlers/LogHandler.h"
#include "handlers/TraceQueryHandler.h"
#include "handlers/DashboardHandler.h"
#include "handlers/ServiceMonitorHandler.h"
#include "handlers/ConfigHandler.h"
#include "handlers/FrontendAssetHandler.h"
#include "core/ServiceRuntimeAccumulator.h"
#include "core/SystemRuntimeAccumulator.h"
#include "core/TraceRetentionService.h"
#include "core/TraceSessionManager.h"
#include "util/DbPathResolver.h"
#include "util/DevSubprocessManager.h"
#include <filesystem>
#include <chrono>
#include <csignal>
#include <iostream>
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

std::optional<std::filesystem::path> ResolveFrontendDistPath(
    const std::optional<std::string>& cli_dist_path,
    const std::filesystem::path& executable_path)
{
    auto normalize = [](const std::filesystem::path& path) -> std::filesystem::path {
        std::error_code ec;
        const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            return normalized;
        }
        return path.lexically_normal();
    };

    if (cli_dist_path.has_value()) {
        const std::filesystem::path cli_path(cli_dist_path.value());
        const std::filesystem::path resolved =
            cli_path.is_absolute()
                ? normalize(cli_path)
                : normalize(std::filesystem::current_path() / cli_path);
        if (std::filesystem::exists(resolved) && std::filesystem::is_directory(resolved)) {
            return resolved;
        }
        return std::nullopt;
    }

    // 默认探测先以可执行文件目录为锚点，再补当前工作目录候选：
    // 这样从项目根直接跑 `./server/build/LogSentinel`，以及先 `cd server/build` 再运行，
    // 最终都会指向同一份 `client/dist`，不再让“当前 cwd 不同”把静态资源路径搞漂。
    const std::filesystem::path executable_dir = executable_path.parent_path();
    const std::vector<std::filesystem::path> candidates = {
        executable_dir / ".." / ".." / "client" / "dist",
        std::filesystem::current_path() / "client" / "dist",
        std::filesystem::current_path() / ".." / "client" / "dist",
    };
    for (const auto& candidate : candidates) {
        const std::filesystem::path resolved = normalize(candidate);
        if (std::filesystem::exists(resolved) && std::filesystem::is_directory(resolved)) {
            return resolved;
        }
    }
    return std::nullopt;
}

bool StartsWith(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool IsApiPrefixedPath(const std::string& path)
{
    return path == "/api" || StartsWith(path, "/api/");
}

std::string StripApiPrefix(const std::string& path)
{
    if (!IsApiPrefixedPath(path)) {
        return path;
    }
    if (path.size() == 4) {
        return "/";
    }
    return path.substr(4);
}

ProviderProfile ResolveProviderProfileOrDefault(const SystemConfigPtr& snapshot,
                                                const std::string& provider)
{
    if (snapshot) {
        const auto profile = snapshot->resolveProviderProfile(provider);
        if (profile.has_value()) {
            return profile.value();
        }
    }
    // 如果库里缺了某个 provider profile，就给 main.cpp 一个可诊断的空 profile。
    // 这不是老库迁移兼容，而是防止配置表被手工删坏时直接空指针；后续日志会打印 model/key 是否为空。
    return ProviderProfile{provider, "", ""};
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

std::optional<TraceSessionManager::TraceLifecycleProfile> ParseTraceLifecycleProfile(std::string value)
{
    value = ToLowerCopy(std::move(value));
    if (value.empty() || value == "protected") {
        return TraceSessionManager::TraceLifecycleProfile::Protected;
    }
    if (value == "minimal") {
        return TraceSessionManager::TraceLifecycleProfile::Minimal;
    }
    return std::nullopt;
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
    bool db_path_explicit = false;
    int port = 8080;
    bool port_explicit = false;
    // 当前处于 TraceExplorer 联调阶段，开发时默认自动拉起本地 AI proxy，
    // 这样直接点运行就能看到 trace_analysis，不需要每次手敲 --auto-start-proxy。
    // 后续如果要切回更保守的默认行为，再按阶段调整。
    bool auto_start_proxy = true;
    bool auto_start_webhook_mock = false;
    std::string trace_ai_provider = "mock";
    std::string trace_ai_base_url = "http://127.0.0.1:8001";
    // 默认超时先抬到 30s。
    // 真实 GLM 链路在免费额度、冷启动或网络波动下首包可能明显慢于 mock/gemini，本地 10s 太激进。
    int trace_ai_timeout_ms = 90000;
    bool trace_ai_timeout_explicit = false;
    int trace_sweep_interval_ms = 500;
    bool trace_sweep_interval_explicit = false;
    int trace_idle_timeout_ms = 5000;
    bool trace_idle_timeout_explicit = false;
    int server_io_threads_override = -1;
    int worker_threads_override = -1;
    int dispatch_worker_threads_override = -1;
    int worker_queue_size = 10000;
    int trace_capacity = 100;
    bool trace_capacity_explicit = false;
    int trace_token_limit = 0;
    bool trace_token_limit_explicit = false;
    int trace_max_dispatch_per_tick = 64;
    int trace_buffered_span_limit = 4096;
    int trace_active_session_limit = 1024;
    int trace_sealed_grace_window_ms_override = -1;
    int trace_primary_flush_span_threshold = -1;
    int trace_primary_flush_interval_ms = -1;
    int service_monitor_window_minutes = 30;
    int service_monitor_bucket_seconds = 3;
    std::string webhook_provider;
    std::string webhook_url;
    std::string webhook_secret;
    std::optional<std::string> frontend_dist_arg;
    bool disable_ai_cli = false;
    bool disable_webhook_cli = false;
    bool disable_buffered_trace_repo_cli = false;
    std::optional<std::string> trace_lifecycle_profile_cli_override;
    bool trace_ai_provider_explicit = false;
    //简单的命令行参数解析
    // 支持格式: ./LogSentinel --db <path> --port <port> [--auto-start-deps]
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
            db_path_explicit = true;
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
            trace_ai_timeout_explicit = true;
        } else if (arg == "--trace-sweep-interval-ms" && i + 1 < argc) {
            trace_sweep_interval_ms = std::stoi(argv[++i]);
            trace_sweep_interval_explicit = true;
        } else if (arg == "--trace-idle-timeout-ms" && i + 1 < argc) {
            trace_idle_timeout_ms = std::stoi(argv[++i]);
            trace_idle_timeout_explicit = true;
        } else if (arg == "--server-io-threads" && i + 1 < argc) {
            // benchmark / 演示脚本更关心“服务器 I/O 线程”这个外部语义，
            // 不需要知道内部配置字段叫 kernel_io_threads。
            // 这里单独给一个 CLI override，只改冷启动线程模型，不碰 SQLite 保存值。
            server_io_threads_override = std::stoi(argv[++i]);
        } else if (arg == "--worker-threads" && i + 1 < argc) {
            worker_threads_override = std::stoi(argv[++i]);
        } else if (arg == "--dispatch-worker-threads" && i + 1 < argc) {
            // dispatch 线程数只服务“主数据准备阶段”的并行度对照。
            // 这里和 worker 分开，是为了让 benchmark 能单独观察 dispatch 是否先成为瓶颈。
            dispatch_worker_threads_override = std::stoi(argv[++i]);
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
        } else if (arg == "--trace-sealed-grace-window-ms" && i + 1 < argc) {
            // 这个 override 只服务 benchmark：
            // 它临时改的是 protected 生命周期里 sealed grace 的长度，
            // 目标是评估“为了吸收晚到 span 付出的固定等待”到底有多大。
            trace_sealed_grace_window_ms_override = std::stoi(argv[++i]);
        } else if (arg == "--trace-primary-flush-span-threshold" && i + 1 < argc) {
            // 这两个参数只服务 benchmark：
            // 它们临时改变的是 BufferedTraceRepository 主数据桶的 flush 时机，
            // 不是正式产品配置，不写回 SQLite，也不进入 Settings 页面。
            trace_primary_flush_span_threshold = std::stoi(argv[++i]);
        } else if (arg == "--trace-primary-flush-interval-ms" && i + 1 < argc) {
            trace_primary_flush_interval_ms = std::stoi(argv[++i]);
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
        } else if (arg == "--frontend-dist" && i + 1 < argc) {
            // 单入口部署优先允许黑盒和演示脚本显式指定 dist 目录，
            // 这样测试时可以喂一个临时目录，不需要依赖本地已经手工跑过 `npm run build`。
            frontend_dist_arg = argv[++i];
        } else if (arg == "--disable-ai") {
            // benchmark 专用 CLI 开关优先级必须高于 Settings：
            // 否则你明明是想做“关 AI”的对比实验，却还得先去改库里的 ai_analysis_enabled，
            // 实验变量就会和产品配置语义搅在一起。
            disable_ai_cli = true;
        } else if (arg == "--disable-webhook") {
            // webhook 的 benchmark 开关也按同样口径处理：
            // 只服务启动期对比实验，不进入正式 Settings 产品面。
            disable_webhook_cli = true;
        } else if (arg == "--disable-buffered-trace-repo") {
            // 这条开关的目标不是“关掉持久化”，而是把写路径从双缓冲 flush 线程切到同步直写 SQLite。
            // benchmark 做对照时，必须保留同样的 trace/AI 功能，只拿掉“缓冲写入器”这一层变量。
            disable_buffered_trace_repo_cli = true;
        } else if (arg == "--trace-lifecycle-profile" && i + 1 < argc) {
            // 这条开关只服务 benchmark / 黑盒实验：
            // 它的职责是让我们在“不改 SQLite 产品配置”的前提下，临时把生命周期语义切到另一档。
            // 这样实验变量就能继续只放在启动命令里，不会污染正式 Settings 保存值。
            trace_lifecycle_profile_cli_override = argv[++i];
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
    if (server_io_threads_override == 0 || server_io_threads_override < -1) {
        std::cerr << "Fatal Error: --server-io-threads must be > 0 or omitted" << std::endl;
        return -1;
    }
    if (worker_threads_override == 0 || worker_threads_override < -1) {
        std::cerr << "Fatal Error: --worker-threads must be > 0 or omitted" << std::endl;
        return -1;
    }
    if (dispatch_worker_threads_override == 0 || dispatch_worker_threads_override < -1) {
        std::cerr << "Fatal Error: --dispatch-worker-threads must be > 0 or omitted" << std::endl;
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
    if (trace_sealed_grace_window_ms_override == 0 || trace_sealed_grace_window_ms_override < -1) {
        std::cerr << "Fatal Error: --trace-sealed-grace-window-ms must be > 0 or omitted"
                  << std::endl;
        return -1;
    }
    if (trace_primary_flush_span_threshold == 0 || trace_primary_flush_span_threshold < -1) {
        std::cerr << "Fatal Error: --trace-primary-flush-span-threshold must be > 0 or omitted"
                  << std::endl;
        return -1;
    }
    if (trace_primary_flush_interval_ms == 0 || trace_primary_flush_interval_ms < -1) {
        std::cerr << "Fatal Error: --trace-primary-flush-interval-ms must be > 0 or omitted"
                  << std::endl;
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

    const std::filesystem::path executable_path = DbPathResolver::ResolveCurrentExecutablePath();
    const std::filesystem::path resolved_db_path =
        db_path_explicit
            ? DbPathResolver::ResolveExplicitDatabasePath(db_path, std::filesystem::current_path())
            : DbPathResolver::ResolveDefaultDatabasePath(db_path, executable_path);
    // 启动阶段直接把最终 DB 路径算死并打印出来：
    // 这样“默认路径到底落哪”“用户删的是不是同一份库”这些问题在日志里一眼就能看见。
    db_path = resolved_db_path.string();
    std::cout << "Resolved database path: " << db_path << std::endl;
    const std::optional<std::filesystem::path> frontend_dist_path =
        ResolveFrontendDistPath(frontend_dist_arg, executable_path);
    if (frontend_dist_path.has_value()) {
        std::cout << "Resolved frontend dist path: "
                  << frontend_dist_path->string() << std::endl;
    } else {
        std::cout << "Resolved frontend dist path: <not found, frontend static hosting disabled>"
                  << std::endl;
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
    // retention 目前也按冷启动配置消费：
    // 既然后台清理线程不会在运行时自动重建，那 Settings 里的保留天数至少要在启动时真实吃到。
    // 这一步先只接 days，批大小和周期继续由后端保守默认值控制，不把更多调参面提前暴露出来。
    const int effective_log_retention_days = startup_app_config.log_retention_days;
    // sealed grace 这里额外保留一个 benchmark-only override。
    // 原因不是要把产品配置重新改回 CLI 驱动，而是 Suite A 现在要回答：
    // 为了 protected 生命周期吸收晚到 span，这段固定等待到底值不值。
    // 所以实验可以临时在启动命令里扫 grace，但正式 Settings 语义不变。
    const int effective_sealed_grace_window_ms =
        trace_sealed_grace_window_ms_override > 0
            ? trace_sealed_grace_window_ms_override
            : (startup_app_config.sealed_grace_window_ms > 0
                   ? startup_app_config.sealed_grace_window_ms
                   : 1000);
    const int effective_retry_base_delay_ms =
        startup_app_config.retry_base_delay_ms > 0
            ? startup_app_config.retry_base_delay_ms
            : 500;
    // 生命周期档位也是冷启动配置：
    // 它直接决定 trace 命中结束条件后到底走 sealed grace 还是下一 tick 直接 dispatch，
    // 这种状态机分支一旦运行中切换，就会把进程里同时活着的会话切成两种语义。
    const std::string effective_trace_lifecycle_profile_name =
        trace_lifecycle_profile_cli_override.has_value()
            ? ToLowerCopy(trace_lifecycle_profile_cli_override.value())
            : (startup_app_config.trace_lifecycle_profile.empty()
                   ? "protected"
                   : ToLowerCopy(startup_app_config.trace_lifecycle_profile));
    const auto effective_trace_lifecycle_profile =
        ParseTraceLifecycleProfile(effective_trace_lifecycle_profile_name);
    // trace_end 主字段和别名现在也归到冷启动配置：
    // 它们决定的是上报 JSON 该怎么解释，不适合在运行中随手切换；否则同一份部署前后两批请求
    // 会因为设置页刚好被改过而使用不同解析口径，排查起来只会更乱。
    const std::string effective_trace_end_field =
        startup_app_config.trace_end_field.empty()
            ? "trace_end"
            : startup_app_config.trace_end_field;
    const std::vector<std::string> effective_trace_end_aliases =
        startup_app_config.trace_end_aliases;
    // Trace AI 的固定系统 prompt、语言约束和业务 guidance 本轮统一按冷启动配置收口。
    // 既然 prompts + active_prompt_id + ai_language 已经被定义成“保存后重启生效”，
    // 那这里就在启动时把最终 trace prompt 模板一次性渲染好，后面 provider 直接复用这份缓存。
    const std::string effective_trace_prompt_template =
        BuildTracePromptTemplate(startup_app_config.ai_language,
                                 startup_config_snapshot->active_prompt);
    // trace AI 的 provider 路由仍然按冷启动配置收口。
    // 但 model/api_key 不再放在 app_config 里，而是按 provider 从 ai_provider_profiles 解析；
    // 这样用户把 provider 从 gemini 切到 deepseek 时，不会继续带着 Gemini 的 model/key 去请求 DeepSeek。
    const std::string effective_trace_ai_provider =
        trace_ai_provider_explicit
            ? trace_ai_provider
            : (!startup_app_config.ai_provider.empty()
                   ? startup_app_config.ai_provider
                   : trace_ai_provider);
    const ProviderProfile effective_trace_ai_profile =
        ResolveProviderProfileOrDefault(startup_config_snapshot, effective_trace_ai_provider);
    const std::string effective_trace_ai_model = effective_trace_ai_profile.model;
    const std::string effective_trace_ai_api_key = effective_trace_ai_profile.api_key;
    // AI 调用超时也必须走冷启动配置。
    // 否则前端把 ai_timeout_ms 改成 30000，看起来已经保存成功，但后端实际还在沿用硬编码 10s，就会变成假配置。
    const int effective_trace_ai_timeout_ms =
        trace_ai_timeout_explicit ? trace_ai_timeout_ms : startup_app_config.ai_timeout_ms;
    const bool effective_ai_analysis_enabled = startup_app_config.ai_analysis_enabled;
    // 自动降级这一刀也按冷启动配置消费：
    // 既然主/备 provider 都是在启动时构对象，那 provider/model/api_key 三元组自然也不能运行中热切。
    const bool effective_ai_auto_degrade = startup_app_config.ai_auto_degrade;
    const std::string effective_ai_fallback_provider = startup_app_config.ai_fallback_provider;
    const ProviderProfile effective_ai_fallback_profile =
        ResolveProviderProfileOrDefault(startup_config_snapshot, effective_ai_fallback_provider);
    const std::string effective_ai_fallback_model = effective_ai_fallback_profile.model;
    const std::string effective_ai_fallback_api_key = effective_ai_fallback_profile.api_key;
    const bool effective_ai_circuit_breaker = startup_app_config.ai_circuit_breaker;
    const int effective_ai_failure_threshold =
        startup_app_config.ai_failure_threshold > 0
            ? startup_app_config.ai_failure_threshold
            : 5;
    const int effective_ai_cooldown_seconds =
        startup_app_config.ai_cooldown_seconds > 0
            ? startup_app_config.ai_cooldown_seconds
            : 60;
    const int64_t effective_ai_cooldown_ms =
        static_cast<int64_t>(effective_ai_cooldown_seconds) * 1000;
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
    if (!effective_trace_lifecycle_profile.has_value()) {
        std::cerr << "Fatal Error: unsupported trace_lifecycle_profile '"
                  << effective_trace_lifecycle_profile_name
                  << "', expected protected|minimal" << std::endl;
        return -1;
    }
    if (effective_ai_failure_threshold <= 0) {
        std::cerr << "Fatal Error: effective ai_failure_threshold must be > 0" << std::endl;
        return -1;
    }
    if (effective_ai_cooldown_seconds <= 0) {
        std::cerr << "Fatal Error: effective ai_cooldown_seconds must be > 0" << std::endl;
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
    if (auto_start_proxy && !disable_ai_cli) {
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

    if (auto_start_webhook_mock && !disable_webhook_cli) {
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

    std::shared_ptr<SqliteTraceRepository> trace_repo;
    std::shared_ptr<SqliteTraceRepository> trace_read_repo;
    std::shared_ptr<TraceWriteSink> trace_write_sink;
    std::shared_ptr<BufferedTraceRepository> buffered_trace_repo;
    try
    {
        trace_repo = std::make_shared<SqliteTraceRepository>(db_path);
        trace_read_repo = std::make_shared<SqliteTraceRepository>(db_path);
        if (disable_buffered_trace_repo_cli) {
            // no-buffer 对照组直接复用同一个 SQLite repo 做同步写入。
            // 这样改掉的是“写入口实现”，不是 trace 表结构或查询口径。
            trace_write_sink = std::make_shared<DirectTraceWriteSink>(trace_repo);
        } else {
            // 这里仍然只构建产品里的 BufferedTraceRepository。
            // benchmark-only CLI 做的事情只是临时覆盖“主数据桶多大/多久 flush 一次”，
            // 不会把这些实验参数写进 SQLite 配置仓库，也不会改变其它环境的默认行为。
            BufferedTraceRepository::Config buffered_trace_config;
            if (trace_primary_flush_span_threshold > 0) {
                buffered_trace_config.primary_span_reserve =
                    static_cast<size_t>(trace_primary_flush_span_threshold);
            }
            if (trace_primary_flush_interval_ms > 0) {
                buffered_trace_config.primary_flush_interval_ms =
                    static_cast<int64_t>(trace_primary_flush_interval_ms);
            }
            buffered_trace_repo =
                std::make_shared<BufferedTraceRepository>(trace_repo, buffered_trace_config);
            trace_write_sink = buffered_trace_repo;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal Error:Failed to initialize persistence layer" << e.what() << '\n';
        return -1;
    }

    const int num_cpu_cores = std::thread::hardware_concurrency();
    // I/O 线程和主 worker 线程现在分别由两个冷启动配置控制。
    // 这样 Settings 里改线程模型时，用户就能明确知道：
    // - `kernel_io_threads` 影响的是 MiniMuduo 收包/分发这一层；
    // - `kernel_worker_threads` 影响的是 Trace 聚合、AI 调用和落库协作这一层。
    const int configured_io_threads =
        server_io_threads_override > 0
            ? server_io_threads_override
            : (startup_app_config.kernel_io_threads > 0
                   ? startup_app_config.kernel_io_threads
                   : 1);
    const int num_io_threads = configured_io_threads;
    const int detected_cpu_cores = num_cpu_cores > 0 ? num_cpu_cores : 1;
    int default_worker_threads = detected_cpu_cores - num_io_threads;
    if (default_worker_threads <= 0) {
        default_worker_threads = 1;
    }
    // worker 线程数和端口一样属于冷启动参数：
    // 既然线程池创建后不会在运行中自动扩缩，那么这里就只在启动时做一次“CLI > Settings > 默认值”的决策。
    const int num_worker_threads =
        worker_threads_override > 0
            ? worker_threads_override
            : (startup_app_config.kernel_worker_threads > 0
                   ? startup_app_config.kernel_worker_threads
                   : default_worker_threads);
    // dispatch 线程数同样按冷启动参数决策：
    // 这层线程承担的是 dispatch queue 消费和主数据准备，不会在运行中自动扩缩。
    const int num_dispatch_threads =
        dispatch_worker_threads_override > 0
            ? dispatch_worker_threads_override
            : (startup_app_config.dispatch_worker_threads > 0
                   ? startup_app_config.dispatch_worker_threads
                   : 1);
    const int num_query_threads = 1;
    const BufferedTraceRepository::Config default_buffer_config;
    const size_t effective_trace_primary_flush_span_threshold =
        trace_primary_flush_span_threshold > 0
            ? static_cast<size_t>(trace_primary_flush_span_threshold)
            : default_buffer_config.primary_span_reserve;
    const int64_t effective_trace_primary_flush_interval_ms =
        trace_primary_flush_interval_ms > 0
            ? static_cast<int64_t>(trace_primary_flush_interval_ms)
            : default_buffer_config.primary_flush_interval_ms;

    std::cout << "System Info: " << num_cpu_cores << " cores detected." << std::endl;
    std::cout << "Thread Model: " << num_io_threads << " I/O threads, "
              << num_worker_threads << " worker threads, "
              << num_dispatch_threads << " dispatch threads, "
              << num_query_threads << " query threads." << std::endl;
    std::cout << "Benchmark switches: disable_ai=" << (disable_ai_cli ? "true" : "false")
              << ", disable_webhook=" << (disable_webhook_cli ? "true" : "false")
              << ", disable_buffered_trace_repo=" << (disable_buffered_trace_repo_cli ? "true" : "false")
              << ", trace_lifecycle_profile_override="
              << (trace_lifecycle_profile_cli_override.has_value()
                      ? ToLowerCopy(trace_lifecycle_profile_cli_override.value())
                      : "<none>")
              << ", sealed_grace_window_ms_override="
              << (trace_sealed_grace_window_ms_override > 0
                      ? std::to_string(trace_sealed_grace_window_ms_override)
                      : "<none>")
              << std::endl;
    std::cout << "Trace persistence mode: "
              << (disable_buffered_trace_repo_cli ? "direct/no-buffer" : "buffered")
              << std::endl;
    if (!disable_buffered_trace_repo_cli) {
        std::cout << "Buffered trace primary flush: span_threshold="
                  << effective_trace_primary_flush_span_threshold
                  << ", interval_ms=" << effective_trace_primary_flush_interval_ms
                  << std::endl;
    }
    MiniMuduo::net::EventLoop loop;
    MiniMuduo::net::InetAddress addr(effective_port);
    testServer server(&loop, addr, num_io_threads);
    std::vector<WebhookChannel> webhook_channels =
        BuildWebhookChannelsFromSettings(startup_config_snapshot->channels);
    if (auto_start_webhook_mock && !disable_webhook_cli) {
        // 开发时自动注入本地 mock webhook，避免“服务已经拉起，但压根没有通知目标”的调试盲区。
        // 这里显式按 generic 渠道注入，是为了让 mock webhook 和真实飞书 webhook 可以并存，而不是互相覆盖。
        webhook_channels.push_back(WebhookChannel{"generic", "http://127.0.0.1:9999/webhook", true, "", "critical"});
    }
    if (!webhook_url.empty() && !disable_webhook_cli) {
        // CLI 直连入口现在降级成调试兜底：
        // settings.channels 已经会在启动时进入 notifier，这里只额外补一个手工 override，
        // 保住现有脚本和答辩临时联调入口，不要求每次都先去页面里保存。
        webhook_channels.push_back(WebhookChannel{webhook_provider, webhook_url, true, webhook_secret, "critical"});
    }
    if (disable_webhook_cli) {
        // benchmark 开关打开时，Settings 渠道配置和 CLI webhook override 都统一失效。
        // 这样黑盒和 wrk 脚本只改启动参数，就能得到“同一套 trace 分析、但完全不外发通知”的对照组。
        std::cout << "Webhook notifier disabled by CLI benchmark switch (--disable-webhook)." << std::endl;
    } else if (!webhook_channels.empty()) {
        std::cout << "Webhook notifier enabled. channels=" << webhook_channels.size() << std::endl;
        for (const auto& channel : webhook_channels) {
            std::cout << "  - provider=" << channel.provider
                      << ", url=" << channel.webhook_url
                      << ", threshold=" << channel.threshold
                      << ", enabled=" << (channel.enabled ? "true" : "false")
                      << std::endl;
        }
    }
    std::shared_ptr<INotifier> notifier;
    if (!disable_webhook_cli) {
        notifier = std::make_shared<WebhookNotifier>(std::move(webhook_channels));
    }
    std::shared_ptr<TraceAiProvider> trace_ai;
    std::shared_ptr<TraceAiProvider> fallback_trace_ai;
    // `--no-auto-start-proxy` 的语义应该只是“不要替我拉起 Python sidecar”，
    // 不能顺手把整个 Trace AI 主链也关掉。
    // 否则像黑盒测试这种“后端打本地 fake proxy、但不需要真实 sidecar”的场景会被误判成 AI disabled，
    // Settings 里的 ai_provider/fallback_provider 看起来已经保存成功，实际根本没有进入冷启动消费链。
    const bool enable_trace_ai = effective_ai_analysis_enabled && !disable_ai_cli;
    if (enable_trace_ai) {
        TraceAiBackend backend = TraceAiBackend::Mock;
        if (!TryParseTraceAiBackend(effective_trace_ai_provider, &backend)) {
            std::cerr << "Fatal Error: unsupported --trace-ai-provider '"
                      << effective_trace_ai_provider << "'. expected one of: mock|gemini|glm|deepseek" << std::endl;
            return -1;
        }
        TraceAiFactoryOptions options;
        options.base_url = trace_ai_base_url;
        options.backend = backend;
        options.timeout_ms = effective_trace_ai_timeout_ms;
        options.prompt_template = effective_trace_prompt_template;
        options.model = effective_trace_ai_model;
        options.api_key = effective_trace_ai_api_key;
        // retry 配置跟主 provider 一起在冷启动阶段固化。
        // 这样 TraceSessionManager 后面只面对“单次 AnalyzeTrace 调用”，不需要自己再关心某家 provider 应不应该重试几次。
        options.retry_enabled = startup_app_config.ai_retry_enabled;
        options.retry_max_attempts = startup_app_config.ai_retry_max_attempts;
        // provider 路由仍然是冷启动决定，但请求体里的 model/api_key 可以在运行中热更新。
        // 这里把 repo 的版本读取和凭证读取函数一起交给 provider，
        // 让它只在版本变化时刷新本地小快照，不把整份 Settings 热路径化。
        options.runtime_version_reader = [config_repo]() -> uint64_t {
            return config_repo->getTraceAiRuntimeVersion();
        };
        options.runtime_credentials_reader = [config_repo, effective_trace_ai_provider]() -> TraceAiRuntimeCredentials {
            const auto snapshot = config_repo->getSnapshot();
            const auto profile = snapshot ? snapshot->resolveProviderProfile(effective_trace_ai_provider) : std::nullopt;
            return TraceAiRuntimeCredentials{
                profile ? profile->model : "",
                profile ? profile->api_key : "",
            };
        };
        trace_ai = CreateTraceAiProvider(options);
        if (effective_ai_auto_degrade) {
            TraceAiBackend fallback_backend = TraceAiBackend::Mock;
            // fallback 也按同一套冷启动工厂构造，区别只在 backend/model/api_key。
            // 这样主/备两路 provider 都保持“单次调用对象”的边界，不把动态切路由塞进 provider 内部。
            if (!TryParseTraceAiBackend(effective_ai_fallback_provider, &fallback_backend)) {
                std::cerr << "Fatal Error: unsupported ai_fallback_provider '"
                          << effective_ai_fallback_provider << "'. expected one of: mock|gemini|glm|deepseek"
                          << std::endl;
                return -1;
            }
            TraceAiFactoryOptions fallback_options;
            fallback_options.base_url = trace_ai_base_url;
            fallback_options.backend = fallback_backend;
            fallback_options.timeout_ms = effective_trace_ai_timeout_ms;
            fallback_options.prompt_template = effective_trace_prompt_template;
            fallback_options.model = effective_ai_fallback_model;
            fallback_options.api_key = effective_ai_fallback_api_key;
            // fallback 走的是同一套重试预算语义，只是 provider/model/api_key 三元组不同。
            // 这里不额外拆一套 fallback_retry_*，先把“每个 provider 调用链都共享自己的 timeout 总预算”这个主语义做实。
            fallback_options.retry_enabled = startup_app_config.ai_retry_enabled;
            fallback_options.retry_max_attempts = startup_app_config.ai_retry_max_attempts;
            // fallback provider 也只热更新请求体里的 model/api_key，不热更新路由本身。
            // 所以这里复用同一条版本号链，但 reader 读取的是 fallback 那组字段。
            fallback_options.runtime_version_reader = [config_repo]() -> uint64_t {
                return config_repo->getTraceAiRuntimeVersion();
            };
            fallback_options.runtime_credentials_reader = [config_repo, effective_ai_fallback_provider]() -> TraceAiRuntimeCredentials {
                const auto snapshot = config_repo->getSnapshot();
                const auto profile = snapshot ? snapshot->resolveProviderProfile(effective_ai_fallback_provider) : std::nullopt;
                return TraceAiRuntimeCredentials{
                    profile ? profile->model : "",
                    profile ? profile->api_key : "",
                };
            };
            fallback_trace_ai = CreateTraceAiProvider(fallback_options);
        }
        std::cout << "Trace AI enabled via proxy. provider=" << effective_trace_ai_provider
                  << ", base_url=" << trace_ai_base_url
                  << ", timeout_ms=" << effective_trace_ai_timeout_ms
                  << ", retry_enabled=" << (startup_app_config.ai_retry_enabled ? "true" : "false")
                  << ", retry_max_attempts=" << startup_app_config.ai_retry_max_attempts
                  << ", ai_language=" << startup_app_config.ai_language
                  << ", model=" << effective_trace_ai_model
                  << ", api_key=" << (effective_trace_ai_api_key.empty() ? "<empty>" : "<configured>")
                  << ", auto_degrade=" << (effective_ai_auto_degrade ? "true" : "false")
                  << ", fallback_provider=" << effective_ai_fallback_provider
                  << ", fallback_model=" << effective_ai_fallback_model
                  << ", fallback_api_key=" << (effective_ai_fallback_api_key.empty() ? "<empty>" : "<configured>")
                  << std::endl;
    } else {
        // 这里区分的是“主链是否真的允许发起 AI 分析”，不是 trace 查询能力本身。
        // 关闭后 summary/spans 仍然照常落库，worker 只会把 ai_status 收成 skipped_manual。
        std::cout << "Trace AI disabled. ai_analysis_enabled="
                  << (effective_ai_analysis_enabled ? "true" : "false")
                  << ", auto_start_proxy=" << (auto_start_proxy ? "true" : "false")
                  << ", disabled_by_cli=" << (disable_ai_cli ? "true" : "false")
                  << ", trace_ai_provider_explicit=" << (trace_ai_provider_explicit ? "true" : "false")
                  << std::endl;
    }
    // 线程池需要在 trace_ai/notifier 之前回收：
    // 既然 worker 任务里拿的是这些对象的裸指针，那么退出时必须先 join worker，
    // 再销毁依赖对象，否则就会在“任务还在跑、对象先析构”时踩悬空指针。
    ThreadPool tpool(num_worker_threads, static_cast<size_t>(worker_queue_size));
    // Trace 读请求单独走查询线程池，避免前端查库任务和 AI/聚合任务抢同一条队列。
    // 当前先固定 1 条查询线程，把“执行通道分离”先做出来，后面再按压测结果调整线程数。
    ThreadPool query_tpool(static_cast<size_t>(num_query_threads), static_cast<size_t>(worker_queue_size));
    TraceRetentionService::Config trace_retention_config;
    trace_retention_config.retention_days = effective_log_retention_days;
    trace_retention_config.batch_size = 500;
    trace_retention_config.max_batches_per_run = 3;
    trace_retention_config.cleanup_interval_ms = 60LL * 60 * 1000;
    auto trace_retention_service = std::make_shared<TraceRetentionService>(
        trace_repo.get(),
        &query_tpool,
        trace_retention_config);
    if (effective_log_retention_days > 0) {
        std::cout << "Trace retention enabled. days=" << effective_log_retention_days
                  << ", batch_size=" << trace_retention_config.batch_size
                  << ", max_batches_per_run=" << trace_retention_config.max_batches_per_run
                  << ", cleanup_interval_ms=" << trace_retention_config.cleanup_interval_ms
                  << std::endl;
        // 启动后先异步清一轮旧数据，避免答辩或联调一打开就被历史过期 trace 污染。
        // 这里仍然只投递到 query_tpool，不在主线程直接删库，防止启动路径被 SQLite 清理阻塞。
        trace_retention_service->TriggerStartupCleanup();
    } else {
        std::cout << "Trace retention disabled. log_retention_days=" << effective_log_retention_days
                  << std::endl;
    }
    auto system_runtime_accumulator = std::make_shared<SystemRuntimeAccumulator>();
    auto service_runtime_accumulator = std::make_shared<ServiceRuntimeAccumulator>(/*service_top_k*/4,
                                                                                  /*operation_top_k*/6,
                                                                                  /*recent_sample_limit*/3,
                                                                                  static_cast<size_t>(service_monitor_window_minutes),
                                                                                  static_cast<size_t>(service_monitor_bucket_seconds));
    std::shared_ptr<TraceSessionManager> trace_session_manager = std::make_shared<TraceSessionManager>(
        &tpool,
        trace_write_sink.get(),
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
        system_runtime_accumulator.get(),
        enable_trace_ai,
        effective_ai_circuit_breaker,
        static_cast<size_t>(effective_ai_failure_threshold),
        effective_ai_cooldown_ms,
        fallback_trace_ai.get(),
        effective_ai_auto_degrade,
        effective_trace_lifecycle_profile.value(),
        static_cast<size_t>(num_dispatch_threads));
    const double trace_sweep_interval_sec =
        static_cast<double>(effective_trace_sweep_interval_ms) / 1000.0;
    std::cout << "Trace session sweep enabled. sweep_interval_ms=" << effective_trace_sweep_interval_ms
              << ", idle_timeout_ms=" << effective_trace_idle_timeout_ms
              << ", sealed_grace_window_ms=" << effective_sealed_grace_window_ms
              << ", retry_base_delay_ms=" << effective_retry_base_delay_ms
              << ", trace_lifecycle_profile=" << effective_trace_lifecycle_profile_name
              << ", max_dispatch_per_tick=" << trace_max_dispatch_per_tick
              << ", trace_capacity=" << effective_trace_capacity
              << ", trace_token_limit=" << effective_trace_token_limit
              << ", wm_active_sessions=" << effective_wm_active_sessions_overload << "/" << effective_wm_active_sessions_critical
              << ", wm_buffered_spans=" << effective_wm_buffered_spans_overload << "/" << effective_wm_buffered_spans_critical
              << ", wm_pending_tasks=" << effective_wm_pending_tasks_overload << "/" << effective_wm_pending_tasks_critical
              << ", ai_analysis_enabled=" << (effective_ai_analysis_enabled ? "true" : "false")
              << ", ai_auto_degrade=" << (effective_ai_auto_degrade ? "true" : "false")
              << ", ai_circuit_breaker=" << (effective_ai_circuit_breaker ? "true" : "false")
              << ", ai_failure_threshold=" << effective_ai_failure_threshold
              << ", ai_cooldown_seconds=" << effective_ai_cooldown_seconds
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
    loop.runEvery(60.0, [trace_retention_service]() {
        if (!trace_retention_service) {
            return;
        }
        const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        trace_retention_service->TrySchedulePeriodicCleanup(now_ms);
    });
    bool shutdown_stats_logged = false;
    loop.runEvery(0.1, [&loop, &shutdown_stats_logged, trace_session_manager_raw, buffered_trace_repo, disable_buffered_trace_repo_cli]() {
        // signal handler 里只能做极少的事情，所以这里只记录退出意图；
        // 真正的 quit 放回 EventLoop 线程执行，这样对象析构和埋点打印才会走完整。
        if (g_shutdown_requested != 0) {
            if (!shutdown_stats_logged) {
                shutdown_stats_logged = true;
                std::clog << "[TraceRuntimeStats] "
                          << trace_session_manager_raw->DescribeRuntimeStats() << std::endl;
                if (buffered_trace_repo) {
                    std::clog << "[BufferedTraceRuntimeStats] "
                              << buffered_trace_repo->DescribeRuntimeStats() << std::endl;
                } else if (disable_buffered_trace_repo_cli) {
                    std::clog << "[BufferedTraceRuntimeStats] disabled_by_cli=true" << std::endl;
                }
            }
            loop.quit();
        }
    });
    
    std::shared_ptr<Router> router = std::make_shared<Router>();
    auto trace_query_handler = std::make_shared<TraceQueryHandler>(trace_read_repo, &query_tpool);
    auto service_monitor_handler = std::make_shared<ServiceMonitorHandler>(service_runtime_accumulator);
    // MVP5 这一步把主路由显式收口到“新 Trace 读写 + 运行态快照 + Settings”。
    // 旧日志分析链已经退出活代码和构造链，主程序不再保留半退役入口混在这里误导联调。

    router->add("POST", "/traces/search", [trace_query_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        trace_query_handler->handleSearchTraces(req, resp, conn);
    });
    router->add("GET", "/traces/*", [trace_query_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        trace_query_handler->handleGetTraceDetail(req, resp, conn);
    });
    router->add("DELETE", "/traces/*", [trace_query_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        trace_query_handler->handleDeleteTrace(req, resp, conn);
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
    router->add("POST", "/settings/provider-profiles", [config_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        // provider profiles 单独走一条接口。
        // 这能把“选择 provider 的冷启动路由”和“该 provider 的 model/api_key 热更新”在 HTTP 契约上拆开。
        config_handler->handleUpdateProviderProfiles(req, resp, conn);
    });
    router->add("POST", "/settings/prompts", [config_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        config_handler->handleUpdatePrompts(req, resp, conn);
    });
    router->add("POST", "/settings/channels", [config_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        config_handler->handleUpdateChannels(req, resp, conn);
    });
    router->add("POST", "/settings/channels/probe", [config_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        config_handler->handleProbeChannel(req, resp, conn);
    });
    // /logs/spans 的结束字段口径已经收口成冷启动配置，
    // 所以这里直接把启动期算好的主字段和别名注入给 LogHandler，不再让请求热路径回头读 repo。
    // LogHandler 现在彻底退化成 `/logs/spans` 专用处理器。
    // 既然旧日志分析链已经从构造链和主路由一起移除，这里只需要注入 Trace 主链依赖即可。
    auto handler = std::make_shared<LogHandler>(trace_session_manager.get(),
                                                system_runtime_accumulator.get(),
                                                effective_trace_end_field,
                                                effective_trace_end_aliases);

    router->add("POST", "/logs/spans", [handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        handler->handleTracePost(req, resp, conn);
    });
    // /dashboard 这一刀正式切到 SystemRuntimeAccumulator 快照。
    // 这样系统监控页先吃到主链路埋点的真值，不再绕回 SQLite 旧 dashboard 统计。
    auto dashboard_handler = std::make_shared<DashboardHandler>(system_runtime_accumulator);
    router->add("GET", "/dashboard", [dashboard_handler](const HttpRequest& req, HttpResponse* resp, const MiniMuduo::net::TcpConnectionPtr& conn) {
        dashboard_handler->handleGetStats(req, resp, conn);
    });

    std::shared_ptr<FrontendAssetHandler> frontend_asset_handler;
    if (frontend_dist_path.has_value()) {
        // 前端页面白名单只保留正式交付入口；
        // 这里不兼容旧地址，避免“一个未知旧路径居然还能返回 index.html”继续把产品边界搞糊。
        frontend_asset_handler = std::make_shared<FrontendAssetHandler>(
            frontend_dist_path->string(),
            std::unordered_set<std::string>{"/", "/service", "/traces", "/settings"});
    }

    auto onRequest=[router, frontend_asset_handler](const HttpRequest& req,
                                                    HttpResponse* resp,
                                                    const MiniMuduo::net::TcpConnectionPtr& conn){
        if (IsApiPrefixedPath(req.path_)) {
            HttpRequest api_request = req;
            api_request.path_ = StripApiPrefix(req.path_);
            if (router->dispatch(api_request, resp, conn)) {
                return;
            }
            // `/api/*` 前缀一旦判定成 API，就绝不能再掉回前端静态资源层。
            // 否则像 `/api/settings` 这种本来就该报接口 404 的路径，会被错误 fallback 成页面，语义直接串味。
            resp->setStatusCode(HttpResponse::HttpStatusCode::k404NotFound);
            resp->addCorsHeaders();
            resp->body_ = "{\"error\": \"404 Not Found\", \"path\": \"" + req.path() + "\"}";
            return;
        }

        if (router->dispatch(req, resp, conn)) {
            return;
        }

        if (frontend_asset_handler && frontend_asset_handler->Handle(req, resp)) {
            return;
        }

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
