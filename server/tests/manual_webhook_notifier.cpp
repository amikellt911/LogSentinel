#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "notification/NotifyTypes.h"
#include "notification/WebhookNotifier.h"

namespace
{
void printUsage(const char* argv0)
{
    std::cout
        << "Usage:\n"
        << "  " << argv0 << " --provider <provider> --webhook-url <url> [options]\n\n"
        << "Options:\n"
        << "  --provider <provider>      目标 provider，例如 feishu / generic\n"
        << "  --webhook-url <url>        真实 webhook 地址；也可通过 LOGSENTINEL_WEBHOOK_URL 环境变量传入\n"
        << "  --webhook-secret <secret>  可选，飞书签名校验 secret；为空时按无签名 webhook 发送\n"
        << "  --trace-id <id>            自定义 trace_id，默认 trace-manual-demo\n"
        << "  --level <level>            风险级别，默认 critical\n"
        << "  --summary <text>           摘要文案\n"
        << "  --root-cause <text>        根因文案\n"
        << "  --solution <text>          建议文案\n"
        << "  --service-name <name>      服务名，可选\n"
        << "  --start-time-ms <ms>       开始时间，默认 1710000000000\n"
        << "  --duration-ms <ms>         持续时间，默认 820\n"
        << "  --span-count <count>       span 数，默认 7\n"
        << "  --token-count <count>      token 数，默认 168\n"
        << "  --confidence <value>       置信度，默认 0.92\n"
        << "  --help                     打印帮助\n";
}

TraceAlertEvent makeDefaultEvent()
{
    // 这里故意构造一条独立的假告警事件，目的是把“飞书消息能不能发通”
    // 从主链 Trace 聚合、Settings、数据库这些变量里解耦出来。
    TraceAlertEvent event;
    event.trace_id = "trace-manual-demo";
    event.service_name = "";
    event.start_time_ms = 1710000000000;
    event.duration_ms = 820;
    event.span_count = 7;
    event.token_count = 168;
    event.risk_level = "critical";
    event.summary = "订单链路在支付阶段出现高风险异常。";
    event.root_cause = "payment-service 调用 bank-gateway 超时。";
    event.solution = "优先检查 payment-service 到 bank-gateway 的网络与重试配置。";
    event.confidence = 0.92;
    return event;
}
} // namespace

int main(int argc, char** argv)
{
    std::string provider = "feishu";
    std::string webhook_url;
    std::string webhook_secret;
    TraceAlertEvent event = makeDefaultEvent();

    if (const char* env_url = std::getenv("LOGSENTINEL_WEBHOOK_URL"))
    {
        webhook_url = env_url;
    }

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto requireValue = [&](const char* flag) -> std::string
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for " << flag << std::endl;
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "--provider")
        {
            provider = requireValue("--provider");
        }
        else if (arg == "--webhook-url")
        {
            webhook_url = requireValue("--webhook-url");
        }
        else if (arg == "--webhook-secret")
        {
            // 这里把 secret 保留在手工联调入口，是为了测试“签名开关打开后的真实飞书机器人”。
            // 如果不传，就继续覆盖无签名机器人路径，方便本地最小联调。
            webhook_secret = requireValue("--webhook-secret");
        }
        else if (arg == "--trace-id")
        {
            event.trace_id = requireValue("--trace-id");
        }
        else if (arg == "--level")
        {
            event.risk_level = requireValue("--level");
        }
        else if (arg == "--summary")
        {
            event.summary = requireValue("--summary");
        }
        else if (arg == "--root-cause")
        {
            event.root_cause = requireValue("--root-cause");
        }
        else if (arg == "--solution")
        {
            event.solution = requireValue("--solution");
        }
        else if (arg == "--service-name")
        {
            event.service_name = requireValue("--service-name");
        }
        else if (arg == "--start-time-ms")
        {
            event.start_time_ms = std::stoll(requireValue("--start-time-ms"));
        }
        else if (arg == "--duration-ms")
        {
            event.duration_ms = std::stoll(requireValue("--duration-ms"));
        }
        else if (arg == "--span-count")
        {
            event.span_count = static_cast<size_t>(std::stoull(requireValue("--span-count")));
        }
        else if (arg == "--token-count")
        {
            event.token_count = static_cast<size_t>(std::stoull(requireValue("--token-count")));
        }
        else if (arg == "--confidence")
        {
            event.confidence = std::stod(requireValue("--confidence"));
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            return 2;
        }
    }

    if (webhook_url.empty())
    {
        std::cerr << "Webhook URL is required. Use --webhook-url or LOGSENTINEL_WEBHOOK_URL." << std::endl;
        printUsage(argv[0]);
        return 2;
    }

    WebhookChannel channel{provider, webhook_url, true, webhook_secret};
    WebhookNotifier notifier(std::vector<WebhookChannel>{channel});

    std::cout << "[manual-webhook] provider=" << provider
              << " webhook_url=" << webhook_url
              << " trace_id=" << event.trace_id
              << " level=" << event.risk_level << std::endl;

    notifier.notifyTraceAlert(event);

    // 这里只要没抛异常、进程能走到结尾，就认为本地发送逻辑已经执行完。
    // 真正是否送达由飞书群消息和服务端返回码共同确认。
    std::cout << "[manual-webhook] send finished" << std::endl;
    return 0;
}
