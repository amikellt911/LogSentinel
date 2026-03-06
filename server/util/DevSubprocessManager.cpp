#include "util/DevSubprocessManager.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
constexpr const char* kLoopback = "127.0.0.1";
}

DevSubprocessManager::~DevSubprocessManager()
{
    // 逆序回收更符合“后启动先退出”的依赖关系，避免链式依赖时出现残留。
    for (auto it = managed_processes_.rbegin(); it != managed_processes_.rend(); ++it) {
        const ManagedProcess& process = *it;
        if (process.pid <= 0) {
            continue;
        }
        if (IsChildExited(process.pid)) {
            continue;
        }
        std::cerr << "[DevDeps] stopping " << process.service_name
                  << " pid=" << process.pid << std::endl;
        TerminateChild(process.pid);
    }
}

bool DevSubprocessManager::EnsurePythonService(const std::string& service_name,
                                               const std::string& script_path,
                                               uint16_t port,
                                               int startup_timeout_ms)
{
    if (IsLocalPortReady(port)) {
        std::cerr << "[DevDeps] " << service_name
                  << " already running on " << kLoopback << ":" << port << std::endl;
        return true;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "[DevDeps] failed to fork for " << service_name
                  << ": " << std::strerror(errno) << std::endl;
        return false;
    }

    if (pid == 0) {
        // 子进程优先尝试 python，失败后回退 python3，兼容不同开发机默认命令。
        ::execlp("python", "python", script_path.c_str(), static_cast<char*>(nullptr));
        ::execlp("python3", "python3", script_path.c_str(), static_cast<char*>(nullptr));
        std::cerr << "[DevDeps] exec failed for script " << script_path
                  << ": " << std::strerror(errno) << std::endl;
        ::_exit(127);
    }

    if (!WaitForLocalPort(port, startup_timeout_ms, pid)) {
        std::cerr << "[DevDeps] " << service_name
                  << " failed to become ready, script=" << script_path << std::endl;
        TerminateChild(pid);
        return false;
    }

    ManagedProcess process;
    process.service_name = service_name;
    process.script_path = script_path;
    process.port = port;
    process.pid = pid;
    managed_processes_.push_back(std::move(process));

    std::cerr << "[DevDeps] started " << service_name
              << " pid=" << pid << " port=" << port << std::endl;
    return true;
}

bool DevSubprocessManager::IsLocalPortReady(uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, kLoopback, &addr.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    const int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::close(fd);
    return rc == 0;
}

bool DevSubprocessManager::WaitForLocalPort(uint16_t port, int timeout_ms, pid_t child_pid)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (IsLocalPortReady(port)) {
            return true;
        }

        // 启动等待期间持续探测子进程存活，避免“进程已崩但仍傻等超时”。
        if (child_pid > 0 && IsChildExited(child_pid)) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return IsLocalPortReady(port);
}

bool DevSubprocessManager::IsChildExited(pid_t pid)
{
    int status = 0;
    const pid_t rc = ::waitpid(pid, &status, WNOHANG);
    return rc == pid;
}

void DevSubprocessManager::TerminateChild(pid_t pid)
{
    if (pid <= 0) {
        return;
    }

    if (::kill(pid, 0) != 0) {
        // 进程已不存在时，不再重复操作。
        return;
    }

    ::kill(pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        const pid_t rc = ::waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 先礼后兵：给足 SIGTERM 时间后仍未退出，再升级为 SIGKILL，避免残留。
    ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
}
