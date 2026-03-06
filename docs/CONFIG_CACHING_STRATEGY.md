# 配置缓存策略 (Configuration Caching Strategy)

本文档针对 `SqliteConfigRepository` 是否需要引入内存缓存进行深度分析。

## 1. 核心疑问
> "要不要加上什么存储，static或者成员变量，免得每次都要重新查询？"

**答案：非常有必要。**

对于配置数据来说，其访问模式通常是 **"读多写极少 (Read-Heavy, Write-Rare)"**。
*   **高频读取:** 每一个日志批次处理 (Batch Processing) 时，AI 模块都需要读取 `API Key`, `Model Name`, `Prompt` 等配置。如果每秒处理 50 个批次，就要查 50 次数据库。
*   **低频写入:** 用户可能几天才改一次配置。

虽然 SQLite 很快（微秒级），但在高并发场景下，频繁的锁争用（即使是读锁）和上下文切换依然是不必要的开销。

---

## 2. 推荐方案：读写锁保护的内存缓存 (Read-Through Cache with R/W Lock)

我们不应该每次都去查 SQLite。建议在 `SqliteConfigRepository` 类中维护一份最新的配置副本。

### 2.1 数据结构设计

在 `SqliteConfigRepository` 类中添加成员变量：

```cpp
#include <shared_mutex> // C++17 读写锁

class SqliteConfigRepository {
private:
    // 缓存对象
    AppConfig cached_app_config_;
    std::vector<PromptConfig> cached_prompts_;
    std::vector<AlertChannel> cached_channels_;
    
    // 读写锁 (比互斥锁更适合读多写少场景)
    // 允许多个 AI 线程同时读，但写的时候互斥
    mutable std::shared_mutex config_mutex_; 
    
    // 标记缓存是否已初始化
    bool is_initialized_ = false;

public:
    // 初始化时加载
    void loadFromDb() {
        std::unique_lock lock(config_mutex_); // 写锁
        // query DB ...
        cached_app_config_ = ...;
        cached_prompts_ = ...;
        is_initialized_ = true;
    }
};
```

### 2.2 读取逻辑 (Read Path)

AI 线程调用 `getAppConfig()` 时：
1.  获取 **共享锁 (Read Lock)**。
2.  直接返回内存中的对象副本（或 const 引用）。
3.  **耗时:** 几乎为零（纳秒级）。

```cpp
AppConfig getAppConfig() {
    // 共享锁：允许多个线程同时进来看
    std::shared_lock lock(config_mutex_); 
    return cached_app_config_;
}
```

### 2.3 写入逻辑 (Write Path)

Settings Handler 调用 `updateConfig()` 时：
1.  获取 **独占锁 (Write Lock)**。
2.  更新 SQLite 数据库。
3.  如果数据库更新成功，同步更新内存中的 `cached_app_config_`。
4.  释放锁。

```cpp
void updateAppConfig(const AppConfig& new_config) {
    std::unique_lock lock(config_mutex_); // 独占锁：甚至阻塞读操作
    
    // 1. 写库 (可能会抛异常)
    writeToDb(new_config);
    
    // 2. 更新缓存
    cached_app_config_ = new_config;
}
```

---

## 3. 为什么不用 Static？

您提到的 *"static"* 变量通常用于跨实例共享。
但在我们的架构中，`SqliteConfigRepository` 本身通常是单例 (Singleton) 或者由 `Server` 持有的唯一 `shared_ptr` 实例。
因此，使用 **成员变量** 就足够了。成员变量随着 Repository 实例存在，这比 Static 更容易控制生命周期（比如在单元测试中可以销毁重建）。

---

## 4. 潜在风险与解决

### 4.1 外部修改 (External Modification)
如果有人直接用 SQLite 客户端修改了 `LogSentinel.db` 文件，内存缓存是感知不到的。
*   **影响:** 程序继续使用旧配置。
*   **解决:** 这是一个嵌入式单体应用，通常假设只有程序自己写数据库。如果确实需要支持外部修改，可以加一个 `POST /api/settings/reload` 接口，强制调用 `loadFromDb()` 刷新缓存。

---

## 5. 总结

引入内存缓存是 **绝对正确的优化方向**。
它将配置读取的开销从 **IO/Syscall 级别** 降低到了 **内存访问级别**，这对于我们在 `docs/PERFORMANCE_BENCHMARK.md` 中计划的高性能跑分至关重要。
