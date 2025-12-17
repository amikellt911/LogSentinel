# 日志仓储优化分析 (Log Repository Optimization)

本文档分析 `SqliteLogRepository` 是否需要采用缓存或锁机制优化。

## 1. 核心差异分析

> "SqliteLogRepository 要不要也进行类似（ConfigRepository）的操作？"

**答案：要，但策略完全不同。**

*   **ConfigRepository:** 数据极少（几十 KB），读多写少。适合 **全量缓存 (Cache-Aside / Read-Through)**。
*   **LogRepository:** 数据海量（GB 级），写多读少（主要读聚合数据）。**绝对不能全量缓存**（内存会爆）。

因此，LogRepository 的优化重点在于 **“缓存聚合结果”** 和 **“消除写锁瓶颈”**。

---

## 2. 缓存策略：只缓存“洞察” (Cache Insights, Not Data)

前端仪表盘 (`/dashboard`) 每秒轮询一次，如果每次都让 SQLite 去扫描全表计算 `COUNT(*)` 或排序 `ORDER BY time DESC`，当日志量达到百万级时，查询会变得非常慢（几百毫秒），甚至阻塞写入。

建议在内存中维护一个 **“热统计层”**：

### 2.1 计数器缓存 (Atomic Counters)
避免执行 `SELECT COUNT(*) FROM logs`。

*   **实现:**
    *   成员变量: `std::atomic<long> total_logs_cache_;`
    *   **初始化:** 启动时查一次 DB，赋值给该变量。
    *   **更新:** 每次 `saveRawLogBatch` 成功后，`total_logs_cache_.fetch_add(batch_size)`。
    *   **读取:** `getDashboardStats` 直接读取该变量（纳秒级）。

### 2.2 最近告警缓存 (Recent Alerts Buffer)
避免执行 `SELECT ... WHERE risk='Critical' ORDER BY time DESC LIMIT 5`。

*   **实现:**
    *   成员变量: `std::deque<AlertInfo> cached_recent_alerts_;` (固定容量 10)
    *   保护锁: `std::mutex alert_cache_mutex_;` (注意：只保护这个 deque，不锁 DB)
    *   **更新:** 写入日志时，如果是高风险，获取锁 -> push_front -> pop_back -> 释放锁。
    *   **读取:** 获取锁 -> 拷贝 vector -> 释放锁。

### 2.3 趋势图表缓存 (Time-Series Buffer)
前端需要的 QPS 曲线、内存曲线。

*   **实现:** 维护一个环形数组 (Ring Buffer)，存储过去 60 秒的 QPS 值。每秒只需读取这个内存数组，完全无需查库。

---

## 3. 锁策略优化 (Locking Strategy)

在 `docs/DATABASE_OPTIMIZATION.md` 中我们提到了 **Thread-Local Connections**。这对 LogRepository 的影响巨大。

### 3.1 现状 (Global Mutex)
```cpp
void saveRawLogBatch(...) {
    std::lock_guard<std::mutex> lock(mutex_); // 锁住整个 Repo
    // 写数据库 IO ... (耗时 10-100ms)
}

DashboardStats getDashboardStats() {
    std::lock_guard<std::mutex> lock(mutex_); // 等待写锁释放
    // 读数据库 ...
}
```
**问题:** 读操作（仪表盘）会被写操作（日志入库）严重阻塞，导致 UI 卡顿。

### 3.2 目标架构 (No DB Mutex)
一旦实现了 Thread-Local 连接 + WAL 模式：

```cpp
void saveRawLogBatch(...) {
    // 1. 获取当前线程的私有 DB 连接 (无锁)
    sqlite3* db = getThreadLocalDb();
    // 2. 写数据库 (WAL模式下，不会阻塞读线程)
    executeInsert(db, ...);

    // 3. 更新内存统计 (轻量级锁/原子操作)
    updateStatsCache(...);
}

DashboardStats getDashboardStats() {
    // 1. 直接读内存缓存 (无 IO，极快)
    stats.total = total_logs_cache_.load();
    stats.alerts = getCachedAlerts(); // 轻量锁

    // 2. 如果必须查库 (比如查 avg_latency)，使用当前线程的私有连接
    // WAL 保证这里不会被 saveRawLogBatch 阻塞
    return stats;
}
```

---

## 4. 总结

1.  **要有缓存，但别存 Log:** 只存 `Count`, `Recent Alerts`, `QPS Metrics`。
2.  **要有锁，但别锁 DB:**
    *   数据库访问靠 **Thread-Local** 实现无锁化并发。
    *   内存缓存访问靠 **Fine-Grained Mutex (细粒度锁)** 或 **Atomics** 保护。

通过这套组合拳，即使后台每秒写入 10,000 条日志，前端的 `/dashboard` 接口也能在 < 1ms 内返回结果，彻底解决“卡顿”问题。
