# 性能竞技场：C++ vs Python 架构对比方案 (Performance Benchmark)

本文档分析如何通过“跑分模式”直观展示本项目的 C++ 高性能架构相对于主流 Python 方案的优势。

## 1. 核心目标
我们不仅要证明 C++ 语言本身更快，更要证明 **“MiniMuduo IO + 双线程池 + 聚合写入”** 这套架构设计的优越性。
通过让用户亲自运行对比测试，让他们看到：
*   **吞吐量 (QPS):** 同样的机器，C++ 能处理多少倍的日志。
*   **延迟稳定性:** 在高并发下，C++ 是否依然顺滑，而 Python 是否出现抖动。
*   **资源效率:** C++ 仅占用极少内存和 CPU。

---

## 2. 竞品选择：Python 对照组
为了让比较“公平”且有说服力，我们不应该拿最慢的 Python 框架（如 Flask 单线程）去比，那样胜之不武。
我们选择 **Python 界的性能标杆：FastAPI + Uvicorn**。

*   **实现方式:** 使用标准的 Python `asyncio` 异步写法。
*   **为什么不帮 Python 写线程池？**
    *   这就回答了您的问题：**不需要帮他们做架构优化**。
    *   Python 的 `asyncio` 是它处理高并发的标准方式。如果因为 **GIL (全局解释器锁)** 导致它在 JSON 解析和逻辑处理上卡顿，或者因为没有精细的线程池隔离导致 IO 阻塞计算，**这正是我们要展示的架构缺陷**。
    *   我们的优势在于：C++ 能利用多核真正并行处理计算任务（解析），而 Python 即使开了多线程也受限于 GIL。

---

## 3. 压测架构设计

我们需要引入一个独立的 **流量生成器 (Traffic Generator)**。

```mermaid
graph TD
    UI[前端界面] -- 1. 选择模式 & 重启 --> Launcher[启动脚本]
    
    subgraph "Mode A: C++ (Our Hero)"
        CppServer[C++ Backend]
        CppPool[IO + Worker Threads]
    end
    
    subgraph "Mode B: Python (The Rival)"
        PyServer[FastAPI Backend]
        PyLoop[Async Event Loop]
    end
    
    Launcher -- 启动 --> CppServer
    Launcher -- 启动 --> PyServer
    
    UI -- 2. 开始跑分 --> Generator[流量生成器 (Mock Sender)]
    Generator -- 3. 高并发 POST /logs --> CppServer
    Generator -- 3. 高并发 POST /logs --> PyServer
    
    CppServer -- 4. 写入报告 --> ResultFile[benchmark_result.json]
    PyServer -- 4. 写入报告 --> ResultFile
```

### 3.1 流量生成器 (The Stressor)
*   **形式:** 一个简单的 Python 脚本（利用 `multiprocessing` 多进程发送）或编译好的 Go 小工具。
*   **行为:** 
    *   死循环发送 `POST /logs`。
    *   不等待响应内容，只管发（Fire and Forget，或者等待 HTTP 200）。
    *   **关键点:** 它的发送能力必须远大于 Python 服务端的处理能力，才能测出瓶颈。

### 3.2 两种模式的实现细节

*   **C++ 模式 (LogSentinel):**
    *   全功能开启：接收 -> JSON解析 -> 聚合 -> 存入 SQLite (WAL)。
    *   优势点: 零拷贝、多线程并发解析、无 GC 暂停。

*   **Python 模式 (Baseline):**
    *   代码结构: `server.py`
    *   逻辑: 接收 -> `json.loads` -> `sqlite3.execute`。
    *   缺陷展示:
        *   **CPU 瓶颈:** JSON 解析和 SQLite 绑定会抢占 GIL，导致事件循环处理请求变慢。
        *   **无法并行:** 即使是 4 核 CPU，Python 主进程也只能用满 1 个核。

---

## 4. 交互流程 (User Journey)

为了避免“同时运行互相抢 CPU”，采用 **分时测试** 策略：

1.  **进入竞技场:** 
    *   前端设置页面 -> 点击“性能跑分模式”。
    *   选择 **"挑战者: Python FastAPI"** -> 点击“切换并重启”。
    *   (界面显示全屏遮罩，等待后端重启为 Python 版本)。

2.  **第一轮测试 (Python):**
    *   重启完成后，显示仪表盘（此时是 Python 后端提供 API）。
    *   点击 **"开始 30秒 压力测试"**。
    *   前端调用后端接口 `/api/benchmark/start`，或者直接启动本地的流量生成器进程。
    *   **30秒后:** 前端获取测试结果（总处理数: 5,000，平均 QPS: 166）。
    *   **记录:** 将此数据暂存在前端 Store 或 LocalStorage。

3.  **第二轮测试 (C++):**
    *   选择 **"卫冕者: C++ LogSentinel"** -> 点击“切换并重启”。
    *   重启完成后，点击 **"开始 30秒 压力测试"**。
    *   **30秒后:** 前端获取测试结果（总处理数: 250,000，平均 QPS: 8,333）。

4.  **结果处刑 (Showdown):**
    *   前端检测到两组数据都已就绪。
    *   弹窗展示对比图表：
        *   **吞吐量:** 8333 vs 166 (C++ 胜出 50 倍)。
        *   **内存:** 20MB vs 150MB。
    *   **总结文案:** "得益于 MiniMuduo 优秀的 IO 多路复用与双线程池架构，C++ 版本在高并发场景下展现了压倒性的性能优势。"

---

## 5. 性能统计方式

正如您建议的，**后端自统计** 是最准确的。

*   **实现:**
    *   后端维护一个原子计数器 `atomic<long> processed_count`。
    *   当收到 `/benchmark/start` 信号时，重置计数器，开始计时。
    *   30秒后，停止统计，将 `processed_count / 30` 计算出 QPS，并写入 `benchmark_result.json` 文件。
    *   前端读取该文件展示结果。

---

## 6. 总结

这个功能将是本项目的“杀手级”演示。它不需要复杂的代码，只需要一个简单的 Python 对照组脚本和一个压测脚本，就能极具视觉冲击力地证明您的架构设计价值。
我们不需要通过给 Python 加线程池来“帮”它，反而应该利用它原生的缺陷（单核、GIL）来衬托 C++ 多线程架构的正确性。
