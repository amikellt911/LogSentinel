# 20260331-feat-wrk-active-pool

## Git Commit Message

feat(benchmark): 新增多活跃 Trace 池的 wrk Lua 压测脚本

## Modification

- server/tests/wrk/trace_model_active_pool.lua
- server/tests/wrk/verify_trace_model_active_pool.py
- docs/todo-list/Todo_Benchmark.md

## Learning Tips

### Newbie Tips

- wrk 是“多线程 + 每线程多连接”的模型，不是“每个连接各跑一份 Lua 状态”。所以如果脚本里只有一个 `current_trace`，本质上只是“每个 wrk 线程维护一条当前 trace”，拟真度会偏粗。
- 想提升拟真度，优先做“每线程 active trace 池”，不要急着做“全局共享状态 + 加锁”。因为压测器自己先抢锁，会把 client 端先测成瓶颈，结果反而更假。
- `setup(thread)` 注入的是线程私有全局变量；如果脚本里把同名变量声明成 `local`，就会把 setup 注入值遮住。这类 bug 不会让 wrk 直接报错，但会把 `thread_id/thread_role` 悄悄打成默认值，特别阴。

### Function Explanation

- `setup(thread)`
  这一步负责给每个 wrk worker 线程分配角色，例如 `end/capacity/timeout`。分配结果不会放进跨线程共享区，而是直接写进对应线程自己的 Lua state。
- `active_traces[slot]`
  这是线程内的活跃 trace 池。每次 `request()` 都按 round-robin 选一个 slot，给那条 trace 推进一个 span；当这条 trace 达到结束条件时，就在同一个槽位原地换一条新 trace。
- `verify_trace_model_active_pool.py`
  这个脚本起一个本地 HTTP 捕获服务，再让 wrk 真跑 Lua 脚本。它不测服务端业务逻辑，只测“请求序列是不是按我们设计的 active pool 状态机生成出来的”。

### Pitfalls

- 不要把“线程角色分配”误做成“连接角色分配”。wrk Lua 层没有稳定的 connection id 让你安全地给每个连接绑一条 trace；更稳的做法是线程内自己调度活跃池。
- 不要假设 capture 到的第一个请求一定是 `slot1/span1`。wrk 启动和连接建立阶段会带来一点点抖动，所以验证脚本应该验证“slot 是否按 round-robin 推进、同一 slot 的 span_id 是否单调递增”，而不是死盯第一条请求。
- 压测脚本升级后，旧的 `trace_model.lua` 仍然要保留。它是已有 benchmark 的基线；如果直接覆盖，后面就说不清是服务端变了，还是流量模型变了。

## Validation

- `python3 server/tests/wrk/verify_trace_model_active_pool.py --case all`
  - 结果：通过，输出 `verify_trace_model_active_pool.py: all checks passed`

---

## 追加记录（Flamegraph harness 接入 active-pool 流量模型）

### Git Commit Message

feat(flamegraph): 支持切换 active-pool wrk 脚本并透传线程角色计划

### Modification

- server/tests/wrk/run_flamegraph.sh
- docs/todo-list/Todo_Benchmark.md
- docs/dev-log/20260331-feat-wrk-active-pool.md

### Learning Tips

#### Newbie Tips

- “脚本升级了”不等于“实验总控已经真的吃到新脚本了”。如果 flamegraph harness 还硬编码旧的 `trace_model.lua`，那你前面做的 active-pool 流量模型在正式实验里根本不会生效。
- warmup 和正式压测必须用同一套流量模型。否则 warmup 热的是老脚本，正式阶段采的是新脚本，最终火焰图会把两套流量口径混在一起。

#### Function Explanation

- `WRK_SCRIPT="${WRK_SCRIPT:-...}"`
  这里把 wrk 脚本路径做成可覆写变量，默认仍指向旧脚本，保证历史实验命令不被这次升级直接改脏；要切新模型时只需要在环境变量里改路径。
- `TRACE_WRK_ROLE_PLAN`
  通过环境变量把线程角色计划透传给 wrk Lua。因为 wrk 的 `setup(thread)` 本来就能给每个线程注入独立角色，所以 harness 只需要负责把这根配置线接通。
- `ACTIVE_POOL_SIZE`
  作为 wrk Lua 的第 5 个位置参数传进去。旧脚本会直接忽略它，新脚本会把它当成每线程 active trace 池大小；这样一套 harness 可以兼容两代脚本。

#### Pitfalls

- 不要把 active-pool 的参数只透传给正式 wrk，不透传给 warmup。那样会造成热身阶段和正式采样阶段的请求形状不一致，火焰图很容易看歪。
- 不要为了接新脚本就直接把 `WRK_SCRIPT` 默认值改掉。保留旧默认值，才不会把之前所有 benchmark/flamegraph 记录的可比性一次性打没。

### Validation

- `bash -n server/tests/wrk/run_flamegraph.sh`
  - 结果：通过
- `rg -n "ACTIVE_POOL_SIZE|TRACE_WRK_ROLE_PLAN|wrk_script=|active_pool_size=|trace_wrk_role_plan=" server/tests/wrk/run_flamegraph.sh`
  - 结果：能看到变量声明、warmup/正式 wrk 透传点，以及 run-summary 输出项
