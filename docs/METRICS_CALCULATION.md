# 系统指标计算指南 (Metrics Calculation)

本文档详细说明了前端仪表盘所需的 **QPS**、**网络延迟** 和 **内存占用** 三大核心指标在 C++ 后端的计算与获取实现方案。

## 1. 吞吐量 (QPS - Queries Per Second)

QPS 代表服务器每秒处理的日志请求数量。

### 1.1 计算原理
采用 **"原子计数 + 定时采样"** 的策略。
1.  维护一个全局的原子计数器 `std::atomic<int> req_counter_`。
2.  每当 `LogHandler` 收到一个 `POST /logs` 请求，`req_counter_++`。
3.  启动一个后台线程（或定时器），**每隔 1 秒**：
    *   读取当前计数值：`current_qps = req_counter_.exchange(0);` (读取并重置为 0)
    *   将 `current_qps` 更新到 `DashboardStats` 缓存中。

### 1.2 代码示例
```cpp
class QpsMonitor {
public:
    void recordRequest() {
        counter_.fetch_add(1, std::memory_order_relaxed);
    }

    int getAndReset() {
        return counter_.exchange(0, std::memory_order_relaxed);
    }
private:
    std::atomic<int> counter_{0};
};

// 在 Server 的定时任务中
void onOneSecondTimer() {
    int qps = qpsMonitor.getAndReset();
    statsCache.updateQps(qps);
}
```

---

## 2. 处理延迟 (Processing Latency)

这里指 **“日志处理耗时”**，即从服务器收到日志到 AI 分析完成入库的总耗时。前端显示的 `avg_response_time` 即为此值。

### 2.1 计算原理
1.  **计时:** 在 `AnalysisTask` 开始前记录 `start_time`，结束后记录 `end_time`。
2.  **差值:** `duration = end_time - start_time` (毫秒)。
3.  **平滑:** 为了防止数值跳动太剧烈，建议使用 **指数加权移动平均 (EWMA)** 算法。
    *   `NewAvg = Alpha * LatestVal + (1 - Alpha) * OldAvg`
    *   `Alpha` 设为 0.1 左右，表示新数据占 10% 权重。

### 2.2 代码示例
```cpp
void updateAvgLatency(double new_latency_ms) {
    // 简单的原子锁保护，或者用原子浮点数操作
    std::lock_guard<std::mutex> lock(mutex_);
    const double alpha = 0.1;
    if (avg_latency_ == 0) {
        avg_latency_ = new_latency_ms;
    } else {
        avg_latency_ = alpha * new_latency_ms + (1.0 - alpha) * avg_latency_;
    }
}
```

---

## 3. 内存占用 (Memory Usage)

需要在后端实时获取当前进程占用的物理内存 (RSS)。

### 3.1 实现原理 (Linux)
读取 `/proc/self/status` 文件，查找 `VmRSS` 字段。

*   **VmRSS (Resident Set Size):** 进程实际占用的物理内存。
*   **计算百分比:** 需要读取 `/proc/meminfo` 获取系统总内存，或者通过 `sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE)` 获取。

### 3.2 代码示例
```cpp
#include <fstream>
#include <string>
#include <unistd.h>

// 获取当前进程内存占用 (MB)
double getCurrentMemoryUsageMB() {
    std::ifstream status_file("/proc/self/status");
    std::string line;
    long rss_kb = 0;
    
    while (std::getline(status_file, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::string val_str = line.substr(7);
            // 移除 " kB" 等后缀并转换
            rss_kb = std::stol(val_str); 
            break;
        }
    }
    return rss_kb / 1024.0;
}

// 获取系统总内存 (MB)
long getTotalSystemMemoryMB() {
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    return (pages * page_size) / (1024 * 1024);
}
```

---

## 4. 整合方案

在 `SqliteLogRepository` (或专门的 `SystemMonitor`) 中：

1.  **QPS:** 依赖 1秒定时器更新。
2.  **Latency:** 每次 AI 任务完成后更新。
3.  **Memory:** 每次前端请求 `/dashboard` 时实时读取 `/proc` 计算（因为它变化不快，且读取文件开销极小）。

这样，前端 polling `/dashboard` 时，拿到的就是最新鲜、最准确的系统内部指标。
