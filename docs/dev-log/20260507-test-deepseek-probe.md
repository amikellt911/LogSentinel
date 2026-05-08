# 20260507-test-deepseek-probe

## Git Commit Message

test(ai): 增加 DeepSeek 手工联通脚本

## Modification

- `server/tests/manual_deepseek_trace_probe.py`
- `docs/todo-list/Todo_TraceAiProvider.md`

## Learning Tips

### Newbie Tips

真实厂商 API 联通脚本不要放进自动测试。它依赖外网、Key、余额、模型可用性和服务商限流，放进 CI 只会制造不稳定失败。

手工探针应该复用生产 provider，而不是另写一套 `requests.post`。否则脚本通了只能证明“脚本自己的 HTTP 写法通”，证明不了系统里的 `DeepSeekProvider` 真能工作。

### Function Explanation

`argparse.ArgumentParser` 用来做命令行参数解析，这里提供 `--api-key`、`--model`、`--base-url`、`--timeout-sec`。

`os.getenv("DEEPSEEK_API_KEY")` 用来从环境变量读取 Key，避免每次都把 Key 写在 shell history 里。

`py_compile` 只做语法和 import 层面的快速检查，不会真正调用 DeepSeek。

### Pitfalls

不要把 API Key 写进仓库、dev-log 或命令示例里的真实值。脚本支持 `DEEPSEEK_API_KEY`，更适合本地临时验证。

DeepSeek JSON mode 不是强 schema，所以脚本除了看 `ok=true`，还会检查 `analysis` 是否含 `summary/risk_level/root_cause/solution`。

## Verification

- `python3 -m py_compile server/tests/manual_deepseek_trace_probe.py`：通过。
- `python3 server/tests/manual_deepseek_trace_probe.py --help`：通过。
- `python3 server/tests/manual_deepseek_trace_probe.py`：按预期退出码 2，并提示缺少 API Key。
- 未执行真实 DeepSeek 联通，因为当前对话没有提供真实 API Key。

# 追加记录：2026-05-07 Trace 详情展示 Span attributes

## Git Commit Message

feat(trace): 在详情页展示 span attributes

## Modification

- `server/persistence/SqliteTraceRepository.h`
- `server/persistence/SqliteTraceRepository.cpp`
- `server/handlers/TraceQueryHandler.cpp`
- `server/tests/SqliteTraceRepository_test.cpp`
- `client/src/types/trace.ts`
- `client/src/views/TraceExplorer.vue`
- `client/src/components/CallChainDrawerContent.vue`
- `docs/todo-list/Todo_TraceReadSide.md`

## Learning Tips

### Newbie Tips

Trace 详情页如果只展示 `service/status/duration`，老师看到的是“调用成功”，看不到 AI 判断所依赖的业务证据。`attributes` 才是这次预售尾款案例里承载 `config_snapshot_version`、`discount_items`、`final_pay_amount` 等事实的地方。

不要把业务结论写进 attributes。前端展示的是事实字段，AI 的 `root_cause` 才是推理结果，两者混在一起会让演示像作弊。

### Function Explanation

`nlohmann::json::parse(...)` 在 `TraceQueryHandler` 中用于把 SQLite 里保存的 `attributes_json` 尝试解析成 JSON 对象。解析失败时不会抛到接口外，而是降级返回 `_raw`。

`Object.entries(...)` 在 `CallChainDrawerContent.vue` 中把 attributes 对象转成 `[key, value]` 列表，方便在 Span 卡片里逐行渲染。

`JSON.stringify(value, null, 2)` 用于把对象或数组格式化成多行文本，避免 `discount_items` 这种字段在页面上挤成一坨。

### Pitfalls

仓库层只透传 `attributes_json`，不要在 `SqliteTraceRepository` 里提前解析业务字段。否则读侧会开始承担业务语义，后面字段一变就会扩散修改。

Handler 解析 attributes 时必须有降级路径。历史库里可能存在非 JSON 或脏数据，如果让它直接抛异常，用户点开一条坏 Trace 就会导致整个详情接口 500。

前端 attributes 展示区不要高亮 `config_snapshot_version` 或 `discount_items` 等“可疑字段”。演示要保持证据中性，让 AI 输出承担判断职责。

## 注释记录

- `server/persistence/SqliteTraceRepository.h`：在 `TraceSpanDetail::attributes_json` 字段上方补充中文注释，说明该字段是读侧透传的 Span 业务属性原文，不参与过滤或推断。
- `server/persistence/SqliteTraceRepository.cpp`：在详情查询读取 `attributes_json` 处补充中文注释，说明详情页需要展示 AI 分析依据，仓库层只负责原样读出。
- `server/handlers/TraceQueryHandler.cpp`：在 attributes 解析与写入详情 JSON 处补充中文注释，说明优先返回 JSON 对象，解析失败降级为 `_raw`，避免脏数据打挂接口。
- `client/src/types/trace.ts`：在 `TraceSpan` 和 `TraceSpanDto` 的 `attributes` 字段补充中文注释，说明该字段用于展示 Span 业务属性证据。
- `client/src/views/TraceExplorer.vue`：在 `mapTraceSpan` 附近补充中文注释，说明 attributes 不参与瀑布图计算，只作为详情证据透传。
- `client/src/components/CallChainDrawerContent.vue`：在 attributes 展示区补充中文注释，说明这里展示的是业务埋点事实，不是 AI 推理结论。

## Verification

- `cmake --build build --target test_sqlite_trace_repo && ./build/test_sqlite_trace_repo --gtest_filter=SqliteTraceRepositoryTest.GetTraceDetailReturnsSummarySpansAndAnalysis`：通过。
- `cmake --build build --target LogSentinel`：通过。
- `npm run build`：未通过，失败在既有 TypeScript 错误，涉及 `AIEngineSearchBar.vue`、`BatchArchiveList.vue`、`BusinessHealthCards.vue`、`PromptDebugger.vue`、`RiskDistribution.vue`、`TraceWaterfall.vue`、`AIEngine.vue`、`Settings.vue`，不在本次 attributes 展示改动点。

# 追加记录：2026-05-08 前端构建与 attributes 黑盒验证收口

## Git Commit Message

fix(frontend): 修复旧组件阻塞前端构建

test(trace): 增加 span attributes 详情黑盒验证

## Modification

- `client/src/components/AIEngineSearchBar.vue`
- `client/src/components/BatchArchiveList.vue`
- `client/src/components/BusinessHealthCards.vue`
- `client/src/components/PromptDebugger.vue`
- `client/src/components/RiskDistribution.vue`
- `client/src/components/TraceWaterfall.vue`
- `client/src/views/AIEngine.vue`
- `client/src/views/Settings.vue`
- `server/tests/smoke_trace_attributes_detail.py`
- `docs/todo-list/Todo_TraceReadSide.md`

## Learning Tips

### Newbie Tips

Vue 路由不引用某个页面，不代表 `vue-tsc` 不检查它。当前 `tsconfig.app.json` 的 include 是 `src/**/*.vue`，所以旧页面和旧组件只要还在 `src` 下，就必须能通过类型检查，否则 `npm run build` 产不出新的 `client/dist`。

后端托管前端静态资源时，如果前端构建失败，浏览器看到的可能还是旧 `dist`，这时页面上残留的旧入口不一定代表源码路由还没改，而是部署产物没有刷新。

### Function Explanation

`defineProps<Props>()` 在 `<script setup>` 中会把 props 暴露给模板；如果脚本里不需要读取 props，就不要再赋值给 `const props`，否则 `noUnusedLocals` 会报错。

`echarts.init(dom, theme, opts)` 的第三个参数是初始化选项，不能放 `backgroundColor`；图表背景应该放在 `setOption` 的 option 里。

`requests.post(..., json=payload)` 会自动序列化 JSON 并设置请求体，适合黑盒脚本发送 `/logs/spans`。

### Pitfalls

不要靠仓库单测证明前后端闭环。仓库测试只能证明 SQLite 读写结构对，不能证明 HTTP Handler、JSON 序列化、前端 DTO 映射都正确。

旧演示组件的 TS 错误不要用关闭严格检查来绕过。现在 v1.0.0 要能重新构建前端，正确做法是让旧文件也保持类型干净。

## 注释记录

- `client/src/components/AIEngineSearchBar.vue`：补充中文注释，说明为什么不再把 `defineProps` 绑定成未使用局部变量。
- `client/src/components/BatchArchiveList.vue`：补充中文注释，说明旧 mock 组件删除未使用风险数组是为了不阻塞正式 dist 构建。
- `client/src/components/BusinessHealthCards.vue`：补充中文注释，说明当前只保留 `ref` 导入，移除无消费者的 `computed`。
- `client/src/components/PromptDebugger.vue`：补充中文注释，说明 `replace` 回调参数显式标注类型只影响展示层高亮。
- `client/src/components/RiskDistribution.vue`：补充中文注释，说明 ECharts 背景配置属于 option，不属于 init opts。
- `client/src/components/TraceWaterfall.vue`：补充中文注释，说明 `renderItem` 当前只需要 api，未使用参数保留为 `_params`。
- `client/src/views/AIEngine.vue`：补充中文注释，说明旧 AIEngine 的 mock debug 数据必须按真实 `PromptDebugger` props 结构组装。
- `client/src/views/Settings.vue`：补充中文注释，说明删除 Prompt 时无需保留未使用的 `deletedPrompt`。
- `server/tests/smoke_trace_attributes_detail.py`：文件顶部和关键流程补充中文注释，说明脚本只验证 `/logs/spans -> /traces/{trace_id}` 的 attributes 证据链，不依赖 AI/Webhook。

## Verification

- `npm run build`：通过。
- `python3 server/tests/smoke_trace_attributes_detail.py`：通过。
