#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <sys/types.h>

// DevSubprocessManager 仅用于本地开发联调场景：
// 在主进程里托管 Python 依赖进程，避免每次手工开多个终端。
class DevSubprocessManager
{
public:
    DevSubprocessManager() = default;
    ~DevSubprocessManager();

    // 确保指定端口对应的 Python 服务可用：
    // 1) 端口已可连时直接复用，不重复拉起；
    // 2) 端口不可连时通过 fork/exec 拉起并等待就绪；
    // 3) 启动失败时返回 false，让上层快速失败，避免主服务带病启动。
    bool EnsurePythonService(const std::string& service_name,
                             const std::string& script_path,
                             uint16_t port,
                             int startup_timeout_ms = 8000);

private:
    struct ManagedProcess
    {
        std::string service_name;
        std::string script_path;
        uint16_t port = 0;
        pid_t pid = -1;
    };

    static bool IsLocalPortReady(uint16_t port);
    static bool WaitForLocalPort(uint16_t port, int timeout_ms, pid_t child_pid);
    static bool IsChildExited(pid_t pid);
    static void TerminateChild(pid_t pid);

    std::vector<ManagedProcess> managed_processes_;
};
