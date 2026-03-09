# 20260309-perf-trace-benchmark

perf(trace): 新增 wrk Trace 模型脚本，按分发路径拆分压测流量

## Git Commit Message
perf(trace): 新增 wrk trace 压测模型脚本

## Modification
- **server/tests/wrk/trace_model.lua**:
  - 新增统一的 wrk Lua 脚本，支持 `end`、`capacity`、`token`、`timeout`、`mixed` 五种 trace 模型。
  - 模板池采用顺序回环，而不是完全随机，保证 benchmark 结果可重复对比。
  - 在脚本内维护 `trace_key/span_id` 的跨请求状态，使多次 HTTP 请求可以共同组成一条 trace。
  - `mixed` 模型采用固定 `70% end / 20% timeout / 10% capacity` 的轮转序列，先做稳定拟真，不把变量撒得过散。
  - 使用 `setup()/init()/request()/done()` 这套 wrk 生命周期回调，把多线程下的 `trace_key` 分片和单线程下的 trace 状态推进都收进脚本本身。
- **docs/todo-list/Todo_Benchmark.md**:
  - 将压测脚本待办从占位的 `trace_standard.lua` 收口为实际落地的 `trace_model.lua`，并标记完成。

## Learning Tips
- **Newbie Tips**:
  - wrk 的 `-t` 是客户端发压线程数，`-c` 是总连接数。单线程完全可以多连接，底层靠非阻塞 socket + 事件循环同时驱动很多连接。
  - Trace 压测和普通日志压测不一样，一条 trace 往往由多次 HTTP 请求共同组成，所以脚本里必须自己维护“当前 trace 发到第几个 span 了”的状态。
- **Function Explanation**:
  - `setup(thread)`: wrk 在创建每个压测线程时自动回调，用来给不同线程注入不同的线程号或种子。
  - `init(args)`: wrk 加载脚本后、每个线程开始工作前调用，用来读取 `mode/spans_per_trace/timeout_cutoff/token_payload_size` 这些参数。
  - `request()`: 每发一个 HTTP 请求前都会调一次，这里才是真正生成 span body 的核心。
  - `done(summary, latency, requests)`: 整轮压测结束后调用，用来打印脚本侧汇总信息。
- **Pitfalls**:
  - 如果所有请求除了 `trace_key` 以外完全一样，benchmark 结果会过于理想化，容易把 CPU cache 和字符串分配路径测假。
  - 如果一上来就把五种分发路径混成随机大杂烩，最后 QPS 或 P99 一差，很难判断到底是 `trace_end`、`capacity` 还是 `timeout` 路径先拖后腿。
  - 多线程 wrk 下如果不给不同线程切不同的 `trace_key` 段，不同线程可能构造出相同的 trace_key，直接把服务端聚合语义搅乱。

## Validation
- 机器上未安装独立 `lua` 解释器，无法直接用 `lua -e` 做语法检查。
- 已执行 `wrk -t1 -c1 -d1s -s server/tests/wrk/trace_model.lua http://127.0.0.1:1 -- end 8 4 2048`，结果进入连接阶段后因目标端口拒绝连接退出，说明 wrk 已成功装载并解析脚本本身。
