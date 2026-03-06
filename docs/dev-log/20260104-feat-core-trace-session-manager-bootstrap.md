# 20260104-feat-core-trace-session-manager-bootstrap

## Git Commit Message
- feat(core): 增加 TraceSessionManager 基础结构占位

## Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager_Bootstrap.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

## Learning Tips
### Newbie Tips
- 先把数据结构的最小版本落地，再逐步补齐行为逻辑，可以降低迭代风险并减少一次性改动范围。

### Function Explanation
- `std::unordered_map` 用于从 trace_key 快速定位到 `std::vector` 下标，减少线性扫描。

### Pitfalls
- 直接暴露 `std::vector` 内部元素指针可能在扩容后失效，先只提供索引或简单查询接口更安全。

## Additional Changes

### Git Commit Message
- feat(core): 调整 TraceSessionManager 的会话持有方式

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 使用 `std::unique_ptr` 可以明确所有权归属，避免共享指针带来的原子计数开销。

#### Function Explanation
- `std::unique_ptr` 支持移动语义，适合把对象所有权转移到线程池任务中处理。

#### Pitfalls
- 直接持有对象值可能在容器扩容时触发搬移，若外部持有指针或引用会失效；使用指针可规避这类隐患。

## Interface Updates

### Git Commit Message
- feat(core): 增加 TraceSessionManager 公共接口占位

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 先定义清晰的接口形态，再补充内部实现，有助于后续按职责拆分代码。

#### Function Explanation
- `Push` 用于接收 span 入口，`Dispatch` 预留给线程池分发流程，当前只占位以确保编译通过。

#### Pitfalls
- 提前暴露复杂实现容易导致接口频繁变更，先用占位接口可以降低返工成本。

## Span Event Updates

### Git Commit Message
- feat(core): 更新 SpanEvent 的结束语义字段

### Modification
- server/core/TraceSessionManager.h
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 使用 `std::optional` 可以明确表达“字段可能缺失”的语义，避免用魔法值混淆逻辑。

#### Function Explanation
- `end_time` 使用 `std::optional<int64_t>`，缺失代表尚未结束或上报不完整。

#### Pitfalls
- 用 `0` 之类的哨兵值可能与真实数据冲突，导致结束判断出错；`optional` 更安全。

## Span Event Fields

### Git Commit Message
- feat(core): 补齐 SpanEvent 核心字段占位

### Modification
- server/core/TraceSessionManager.h
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 统一时间戳的单位能减少跨模块转换成本，避免出现毫秒/秒混用导致的错误。

#### Function Explanation
- 新增 `parent_span_id`、`start_time_ms`、`name`、`service_name` 作为核心字段占位。

#### Pitfalls
- 若 `parent_span_id` 用 0 表示 root，容易与真实 ID 冲突；使用 `optional` 更安全。

## Span Event Attributes

### Git Commit Message
- feat(core): 增加 SpanEvent 扩展字段占位

### Modification
- server/core/TraceSessionManager.h
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 扩展字段先用字符串承载，可以降低解析与存储复杂度，后续再统一升级类型体系。

#### Function Explanation
- 新增 `attributes` 作为扩展字段容器，先支持不固定的日志属性。

#### Pitfalls
- 过早引入复杂类型系统容易影响主线交付，先用简单容器更稳妥。

## Span Status Field

### Git Commit Message
- feat(core): 增加 SpanEvent 状态字段占位

### Modification
- server/core/TraceSessionManager.h
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 用枚举表达有限状态比用字符串更安全，可避免拼写错误导致的逻辑分支遗漏。

#### Function Explanation
- 新增 `Status` 枚举并默认 `Unset`，与 OpenTelemetry 的 `UNSET/OK/ERROR` 语义保持一致。

#### Pitfalls
- 如果用字符串表示状态，后续扩展时容易出现大小写不一致或旧值不兼容的问题。

## Span Status Optional

### Git Commit Message
- feat(core): 调整 SpanEvent 状态字段为可选

### Modification
- server/core/TraceSessionManager.h
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 对于非必填字段，用 `std::optional` 可以减少误用默认值带来的歧义。

#### Function Explanation
- 将 `status` 改为 `std::optional`，未设置表示未显式标记成功或失败。

#### Pitfalls
- 若默认赋值为成功或失败，可能掩盖上游未上报状态的真实情况。

## Span Kind Field

### Git Commit Message
- feat(core): 增加 SpanEvent kind 字段占位

### Modification
- server/core/TraceSessionManager.h
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 使用枚举表达有限角色集合更安全，便于后续统计与可视化。

#### Function Explanation
- 新增 `Kind` 枚举并使用 `std::optional`，用于表达 span 的角色类型。

#### Pitfalls
- 若用字符串表示 kind，容易出现拼写不一致导致聚合结果不稳定。

## Trace Session Container

### Git Commit Message
- feat(core): 增加 TraceSession 的基础容器与去重占位

### Modification
- server/core/TraceSessionManager.h
- docs/todo-list/Todo_TraceSessionManager.md
- docs/KnownIssues.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 将去重集合放进会话结构可以在入口阶段尽早发现异常，避免把问题拖到聚合阶段。

#### Function Explanation
- `spans` 存储按到达顺序的 span，`span_ids` 用于去重，`duplicate_span_id` 保留首个重复的 span_id。

#### Pitfalls
- 若等到线程池聚合再去重，会增加额外扫描成本并延迟异常处理。

## Push Minimal Logic

### Git Commit Message
- feat(core): 增加 TraceSessionManager 的最小 Push 逻辑

### Modification
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 在入口阶段完成去重与建会话，可降低后续线程池任务的复杂度与成本。

#### Function Explanation
- `Push` 会先创建会话，再用 `span_ids` 去重，最后按到达顺序追加 `spans`。

#### Pitfalls
- 如果忽略重复 span 检测，可能导致聚合树出现环或重复节点，影响后续分析结果。

## Trace Session Capacity

### Git Commit Message
- feat(core): 增加 TraceSession 容量构造占位

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/KnownIssues.md
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 预留容量可以减少动态扩容带来的性能抖动，尤其在高频写入场景更明显。

#### Function Explanation
- `TraceSession` 构造时接收容量并预分配 `spans`，`TraceSessionManager` 保存默认容量。

#### Pitfalls
- 若容量配置来源不明确，可能导致预分配过大带来内存浪费；后续需结合配置快照设计。

## Capacity Dispatch Hook

### Git Commit Message
- feat(core): 增加容量触发分发占位逻辑

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 入口侧提前触发分发可以降低单条 Trace 的内存占用压力。

#### Function Explanation
- `Push` 在达到 `capacity` 时调用 `Dispatch`，为后续线程池分发留出挂钩点。

#### Pitfalls
- 若 `Dispatch` 为空实现，可能导致数据不处理；后续需要完善线程池分发逻辑。

## ThreadPool Injection

### Git Commit Message
- feat(core): 增加 TraceSessionManager 的线程池依赖

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 使用裸指针依赖注入可以保持接口简洁，但要确保生命周期由外部统一管理。

#### Function Explanation
- 构造函数注入 `ThreadPool*`，为后续异步分发留出扩展点。

#### Pitfalls
- 若线程池生命周期短于管理器，会导致悬垂指针；需在上层明确对象创建顺序。

## Dispatch Skeleton

### Git Commit Message
- feat(core): 增加 TraceSessionManager 的最小分发骨架

### Modification
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 先做安全的容器删除与索引更新，再接入线程池任务，可以减少调试复杂度。

#### Function Explanation
- `Dispatch` 会移除目标会话并保持 `sessions_` 紧凑，同时更新 `index_by_trace_`。

#### Pitfalls
- 若忘记更新 `index_by_trace_`，后续查找会指向错误下标并引发崩溃。

## Dispatch Comment

### Git Commit Message
- feat(core): 补充 Dispatch 删除策略注释

### Modification
- server/core/TraceSessionManager.cpp
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 删除中间元素时使用 swap+pop_back 能保持 O(1) 删除并减少搬移成本。

#### Function Explanation
- 注释说明了用最后元素覆盖当前索引的删除策略，便于后续维护理解。

#### Pitfalls
- 若忘记同步更新索引映射，会导致逻辑错乱或崩溃。

## Dispatch ThreadPool Hook

### Git Commit Message
- feat(core): 增加 Dispatch 的线程池分发占位

### Modification
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- `std::function` 需要可拷贝的可调用对象，捕获移动语义对象时需注意类型要求。

#### Function Explanation
- Dispatch 中将 `unique_ptr` 转为 `shared_ptr` 以便提交到线程池任务。

#### Pitfalls
- 若线程池为空指针会导致崩溃，因此在提交前应先判空。

## Aggregation Placeholders

### Git Commit Message
- feat(core): 增加 TraceSession 聚合索引与序列化占位

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 先定义聚合与序列化的入口方法，可以避免后续直接在 Dispatch 中堆逻辑。

#### Function Explanation
- 新增 `BuildTraceIndex` 与 `SerializeTrace` 作为后续树构建与输出的占位。

#### Pitfalls
- 如果把聚合与序列化直接写进 Dispatch，容易导致职责膨胀与维护困难。

## TraceIndex Signatures

### Git Commit Message
- feat(core): 补充 TraceIndex 与 DFS 顺序缓存占位

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 将索引构建与序列化拆分，可以减少耦合并便于单独测试每一步。

#### Function Explanation
- 新增 `TraceIndex`、`BuildDfsOrder` 与 `SerializeTrace` 返回值占位。

#### Pitfalls
- 若输出顺序不稳定，序列化结果难以对比与调试；需后续定义统一排序规则。

## BuildTraceIndex Implementation

### Git Commit Message
- feat(core): 完成 TraceIndex 的最小构建逻辑

### Modification
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 先构建 `span_id -> span` 映射，再建立 parent->children，能避免单次遍历中依赖顺序的问题。

#### Function Explanation
- `BuildTraceIndex` 先填充 `span_map`，再构建 `children` 与 `roots`。

#### Pitfalls
- parent 未到达或丢失时应当视为 root，否则会导致子节点丢失。

## BuildDfsOrder Implementation

### Git Commit Message
- feat(core): 增加 DFS 顺序缓存的最小实现

### Modification
- server/core/TraceSessionManager.cpp
- server/core/TraceSessionManager.h
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 使用非递归 DFS 可以避免深层递归导致的栈溢出问题。

#### Function Explanation
- `BuildDfsOrder` 采用栈模拟 DFS，并用 `visited` 防止异常数据引发死循环。

#### Pitfalls
- 若不做去重访问，异常父子关系可能造成无限循环或重复输出。

## SerializeTrace Implementation

### Git Commit Message
- feat(core): 增加 Trace 序列化的最小 JSON 实现

### Modification
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 用 JSON 构建树形结构可以保证层级关系清晰，便于后续调试与扩展。

#### Function Explanation
- `SerializeTrace` 使用递归构建 `children` 数组，并对根节点与子节点按时间与 ID 做稳定排序。

#### Pitfalls
- 若不做排序，输出顺序可能随输入变化，导致调试结果难以对齐。

## SerializeTrace Order Tie-in

### Git Commit Message
- feat(core): 用 DFS 顺序驱动序列化排序

### Modification
- server/core/TraceSessionManager.cpp
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 先生成遍历顺序，再用排序权重控制树形输出，可以同时保留结构与稳定顺序。

#### Function Explanation
- 通过 `rank` 映射将 `order` 转换为排序权重，roots 与 children 按该权重排序。

#### Pitfalls
- 若 `order` 缺失节点，排序回退到 span_id 可能导致顺序与预期不一致。

## SerializeTrace DFS Merge

### Git Commit Message
- feat(core): 合并 DFS 与序列化流程

### Modification
- server/core/TraceSessionManager.h
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 将 DFS 顺序生成与序列化合并，可以减少一次遍历开销。

#### Function Explanation
- `SerializeTrace` 在递归构建 JSON 时同步记录 `order`，避免重复遍历。

#### Pitfalls
- 未排序时输出顺序依赖输入，若需要稳定顺序需在 children 上提前排序。

## SerializeTrace Safety

### Git Commit Message
- feat(core): 序列化阶段增加防环与子节点排序

### Modification
- server/core/TraceSessionManager.cpp
- docs/todo-list/Todo_TraceSessionManager.md
- docs/dev-log/20260104-feat-core-trace-session-manager-bootstrap.md

### Learning Tips
#### Newbie Tips
- 在序列化阶段加防环逻辑可以避免异常数据导致的递归爆炸。

#### Function Explanation
- `SerializeTrace` 维护 `visited` 并对子节点按 `start_time_ms` 排序，保证稳定输出。

#### Pitfalls
- 若排序规则不统一，输出顺序可能随输入变化导致调试困难。
