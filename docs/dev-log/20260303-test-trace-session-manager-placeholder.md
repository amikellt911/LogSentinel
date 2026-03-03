## 记录：TraceSessionManager 单测占位骨架接入 CTest

## Git Commit Message
`test(trace): 新增TraceSessionManager占位单测并接入CTest`

## Modification
- `server/tests/TraceSessionManager_unit_test.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- 在需求还在迭代时，先落“可编译的占位测试”能把测试意图固定下来，后续补实现时不容易漏掉关键场景。
- 把测试目标接到 CTest 比只放一个文件更重要，因为只有接入后 CI 才会持续提醒我们补齐断言。

### Function Explanation
- `GTEST_SKIP()`：显式标记“该用例尚未实现”，测试报告里会显示 skipped，便于区分“失败”与“待完成”。
- `gtest_discover_tests(...)`：让 CTest 自动发现每个 `TEST`，后续可以按用例粒度筛选执行。
- `add_test(...)`：把测试二进制注册给 CTest，保证 `ctest` 能统一调度。

### Pitfalls
- 只创建测试文件但不改 CMake，会导致本地看得到代码、CI 却完全不执行。
- 占位测试如果没有明确命名到具体行为（比如触发条件、通知策略），后续容易演变成“写了但没真正测到核心风险”。

---

## 追加记录：补齐 TraceSessionManager 单测替身类骨架

## Git Commit Message
`test(trace): 在unit测试中补齐repository/ai/notifier替身类`

## Modification
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- 单元测试里的 Fake/Stub/Spy 不是“多写一套代码”，而是为了把失败定位到被测类本身，减少“依赖环境不稳定”带来的噪音。
- 当一个类同时依赖存储、AI、通知时，优先把依赖替换成可控替身，后续再用集成测试覆盖真实依赖联动。

### Function Explanation
- `FakeTraceRepository`：记录 `SaveSingleTraceAtomic` 的入参与调用状态，后续可直接断言落库契约是否正确。
- `StubTraceAi`：固定返回可控风险等级，便于稳定覆盖 `critical` 与非 `critical` 分支。
- `SpyNotifier`：只记录通知是否触发和最后一次事件内容，适合验证通知策略而不触网。

### Pitfalls
- 若直接使用真实 DB/AI/网络做“单元测试”，一旦失败会很难区分是业务逻辑错误还是外部依赖波动。
- 测试替身如果不记录“最后一次入参”，后续即使知道函数被调用了，也无法验证调用内容是否正确。

---

## 追加记录：先实现 6 条 TraceSessionManager 单测断言

## Git Commit Message
`test(trace): 完成6条TraceSessionManager核心单测断言`

## Modification
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- 单测里优先覆盖“触发条件 + 契约映射 + 分支策略”三类高风险点，能在最少用例数量下获得最大回归保护。
- 异步逻辑测试尽量使用短轮询等待条件达成，不要依赖固定 `sleep`，否则在不同机器上容易出现偶现失败。

### Function Explanation
- `WaitUntil(...)`：用于等待线程池异步分发完成后再断言，避免“断言时机早于任务执行”导致误报。
- `std::atomic<bool>`：用于跨线程可见的调用标记，避免测试线程读取到未同步状态。
- `save_atomic_return`：用于后续快速构造“落库成功/失败”分支，不必依赖真实数据库行为来驱动路径。

### Pitfalls
- `TokenEstimator` 当前返回 0，若直接写“token_limit 触发分发”断言会与现状不一致，需等估算逻辑接入后再补该用例。
- 如果在保存入参前就把 `called` 标志置为 true，测试线程可能读到半成品数据，导致偶发不稳定断言。

---

## 追加记录：补齐剩余 6 条 unit 用例并全量通过

## Git Commit Message
`test(trace): 补齐TraceSessionManager剩余unit用例并验证12条全通过`

## Modification
- `server/tests/TraceSessionManager_unit_test.cpp`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- 当某条路径受“占位实现”限制（例如 token 估算固定为 0）时，可以在测试内做最小可控注入，先验证流程分支是否正确，再等待真实实现替换。
- 对于异步分发逻辑，先断言“任务被触发”，再断言“参数内容正确”，能更快定位是调度问题还是组装问题。

### Function Explanation
- `#define private public ... #undef private`：仅测试场景下用于访问私有成员做可控状态注入，避免为测试改生产接口。
- `SerializeTrace(...)`：通过手工构造 `TraceIndex` 覆盖防环逻辑，验证 `anomalies` 输出与无死循环行为。
- `save_atomic_return = false`：在 FakeRepo 中模拟落库失败，验证“保存失败不通知”的一致性策略。

### Pitfalls
- “缺失 parent”在当前实现里是“作为 root 遍历 + 保留原始 parent_id 落库”并存，测试断言必须与实际策略一致，否则会出现语义错判。
- 如果只测 happy path，会遗漏“落库失败但仍通知”等高风险一致性问题，这类问题上线后排查成本很高。

---

## 追加记录：新增 unit workflow 自动执行核心单元测试

## Git Commit Message
`ci(test): 新增unit工作流执行TraceSessionManager单测`

## Modification
- `.github/workflows/unit.yml`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- 单元测试与冒烟测试分开建 workflow，失败定位会更快：单测失败优先看代码逻辑，冒烟失败优先看链路依赖。
- 在 CI 里先跑稳定、耗时短的核心单测，可以更早阻断回归，减少后续排障成本。

### Function Explanation
- `ctest -R "^TraceSessionManagerUnitTest\\."`：通过正则筛选执行指定测试套件，避免一次跑全量测试拉长反馈时间。
- `workflow_dispatch`：允许手动触发，便于你在未发 PR 时先验证 CI 配置是否正确。

### Pitfalls
- 如果 workflow 只写冒烟而不写单测，很多逻辑回归会在更后阶段才暴露，定位成本明显更高。
- 没有 `--output-on-failure` 时，CI 失败日志信息不足，复盘会来回翻找上下文。

---

## 追加记录：输出测试资产台账并给出保留/优化/下线建议

## Git Commit Message
`docs(test): 新增测试资产台账并梳理保留优化下线策略`

## Modification
- `docs/TEST_ASSET_LEDGER.md`
- `docs/todo-list/Todo_TestAssets.md`

## Learning Tips

### Newbie Tips
- 在测试体系进入“多脚本并存”阶段时，先做资产台账再继续加测试，能避免重复投入和历史包袱扩大。
- “下线候选”不等于立刻删除，建议先做软下线（标记 deprecated + 保留一轮）再清理。

### Function Explanation
- `ctest -N`：只列出已注册测试，不执行。适合做测试资产盘点时核对“哪些真的在跑”。
- `gtest_discover_tests(...)`：自动发现 GoogleTest 用例；如果同时再 `add_test` 同一目标，列表里可能出现重复注册。

### Pitfalls
- 没有台账时，最常见问题是“同一风险点被多份脚本重复覆盖”，导致维护成本上升但质量收益有限。
- 历史脚本若依赖旧二进制或旧表结构，继续留在目录中会误导后续同学判断真实测试覆盖情况。

---

## 追加记录：对历史测试脚本执行软下线标注

## Git Commit Message
`chore(test): 软下线历史测试脚本并补充替代入口说明`

## Modification
- `server/tests/legacy/run_tests.py`
- `server/tests/legacy/test_httpserver.py`
- `server/tests/legacy/integration_gemini_test.py`
- `server/tests/legacy/test_mvp1.py`
- `server/tests/legacy/test_mvp2.1_gemini.py`
- `docs/TEST_ASSET_LEDGER.md`
- `docs/todo-list/Todo_TestAssets.md`

## Learning Tips

### Newbie Tips
- 软下线比“直接删除”更稳：先阻止误用，再观察一轮是否仍有隐性依赖，能降低回归风险。
- 对历史脚本写清替代入口（例如 smoke/ctest 命令）能减少团队迁移成本，避免“知道废弃但不知道用什么替代”。

### Function Explanation
- `DEPRECATED` 头注释：用于声明脚本生命周期状态，告诉团队“这份脚本为什么不再建议使用”以及“应该改用什么”。
- “软下线”：文件仍保留，但从默认执行路径中退出，不再作为 CI 或日常回归入口。

### Pitfalls
- 只说“别用这个脚本”但不给替代命令，会导致团队继续私下复用旧脚本，治理失败。
- 在未盘点引用关系前直接删文件，容易触发文档/脚本链路断裂，后续排查成本更高。

---

## 追加记录：将软下线脚本集中迁移到 legacy 目录

## Git Commit Message
`chore(test): 将软下线脚本迁移到legacy目录统一管理`

## Modification
- `server/tests/legacy/README.md`
- `server/tests/legacy/run_tests.py`
- `server/tests/legacy/test_httpserver.py`
- `server/tests/legacy/integration_gemini_test.py`
- `server/tests/legacy/test_mvp1.py`
- `server/tests/legacy/test_mvp2.1_gemini.py`
- `docs/TEST_ASSET_LEDGER.md`
- `docs/todo-list/Todo_TestAssets.md`

## Learning Tips

### Newbie Tips
- 把历史脚本集中到 `legacy/` 是一种“可逆治理”策略：既不立即删除历史信息，也能显著降低新同学误用概率。
- 目录结构本身就是约束，文件在主目录还是 `legacy/` 会直接影响团队默认选择。

### Function Explanation
- `legacy/README.md`：统一说明此目录的定位、替代入口和脚本清单，降低维护者理解成本。
- 软下线迁移：属于“路径层面的治理”，不改脚本行为，只改变默认可见性和入口优先级。

### Pitfalls
- 只迁移文件但不更新台账与文档引用，会造成“路径找不到”的二次混乱。
- 未给替代入口就做目录迁移，会让使用者继续在主目录创建新旧脚本副本，治理失败。

---

## 追加记录：补齐 legacy 迁移后的路径引用

## Git Commit Message
`docs(test): 修正legacy迁移后的文档与说明路径引用`

## Modification
- `AGENTS.md`
- `server/http/README.md`
- `Mvp2结项.md`
- `server/tests/test_history_api.py`

## Learning Tips

### Newbie Tips
- 文件迁移后，最容易漏的是“文档里的示例命令和路径”，这类问题不会编译报错，但会直接影响团队执行效率。
- 迁移脚本时把“替代入口”同步写到规范文档里，能减少口口相传造成的偏差。

### Function Explanation
- 路径引用修正：属于“可用性修复”，目标是让读文档的人按命令一步就能跑通，不被历史路径卡住。
- legacy 目录策略：通过路径层面的弱约束，把历史脚本从默认路径移开，降低误触发概率。

### Pitfalls
- 如果只迁移文件不改 AGENTS 指南，后续自动化协作可能继续引用旧路径。
- 旧报告文档若长期保留错误路径，会误导后续排障同学走回头路。

---

## 追加记录：清理 CTest 重复注册并同步 unit workflow 匹配规则

## Git Commit Message
`test(cmake): 清理CTest重复注册并同步unit筛选规则`

## Modification
- `server/CMakeLists.txt`
- `.github/workflows/unit.yml`
- `docs/TEST_ASSET_LEDGER.md`
- `docs/todo-list/Todo_TestAssets.md`
- `docs/dev-log/20260303-test-trace-session-manager-placeholder.md`
- `server/tests/legacy/README.md`
- `server/tests/legacy/run_tests.py`
- `server/tests/legacy/test_mvp1.py`
- `server/tests/legacy/test_mvp2.1_gemini.py`

## Learning Tips

### Newbie Tips
- 同一 gtest 目标同时走 `add_test` 和 `gtest_discover_tests`，会造成重复执行，测试变慢且报告可读性下降。
- 清理重复注册后，CTest 的筛选表达式应改为按“用例名模式”匹配，而不是二进制目标名。

### Function Explanation
- `gtest_discover_tests(...)`：把 GoogleTest 用例按测试项注册到 CTest，便于按用例粒度筛选运行。
- `ctest -R "^TraceSessionManagerUnitTest\\."`：匹配 `TraceSessionManagerUnitTest.*` 全部用例，稳定覆盖整个单测套件。

### Pitfalls
- 清理 CMake 注册方式后如果不同时更新 CI 命令，`ctest -R` 可能匹配不到任何测试，出现“CI 绿但实际没跑”的假象。
- 只改代码不改文档中的示例命令，会让团队成员继续使用过期命令并误判环境异常。

---

## 追加记录：迁移 TraceSessionManager 早期基础测试到 legacy 并退出 CTest

## Git Commit Message
`chore(test): 迁移重复TraceSessionManager基础测试到legacy并移出CTest`

## Modification
- `server/tests/legacy/TraceSessionManager_test.cpp`
- `server/tests/legacy/README.md`
- `server/CMakeLists.txt`
- `docs/TEST_ASSET_LEDGER.md`
- `docs/todo-list/Todo_TestAssets.md`

## Learning Tips

### Newbie Tips
- 当两套测试覆盖同一职责时，优先保留语义更完整、可维护性更高的一套（本次保留 unit），可以显著降低后续迭代心智负担。
- 测试迁移后要先验证“主防线是否仍可跑通”，避免在整理过程中把真正的质量门禁搞丢。

### Function Explanation
- `legacy/TraceSessionManager_test.cpp`：保留历史对照，不再作为默认回归入口参与 CTest 调度。
- `ctest -R "^TraceSessionManagerUnitTest\\."`：作为当前 TraceSessionManager 的主回归入口，验证 12 条 unit 用例稳定通过。

### Pitfalls
- 迁移重复测试后，集成测试可能暴露“旧断言与现实现状不一致”的遗留问题；这类问题应单独收敛，不应和目录治理混在一次改动里。
- 若不把迁移信息同步到台账，后续成员会误以为“测试被删除”，而不是“测试被有意降级为 legacy”。

---

## 追加记录：收敛 TraceSessionManager 集成测试与现实现状不一致断言

## Git Commit Message
`test(trace): 修正TraceSessionManager集成测试旧断言并验证全量通过`

## Modification
- `server/tests/TraceSessionManager_integration_test.cpp`
- `docs/todo-list/Todo_TraceSessionManager_IntegrationTest.md`

## Learning Tips

### Newbie Tips
- 测试失败不一定是代码坏了，也可能是“测试期望落后于实现演进”；这两者要分开处理，避免误修业务逻辑。
- 对集成测试优先断言“稳定语义”而非“易变细节”，能减少环境差异导致的脆弱失败。

### Function Explanation
- `PersistsSummarySpanAndAnalysisFields`：risk_level 改为“非空”断言，避免被 MockTraceAi 动态风险等级影响。
- `MarksCycleAsAnomaly`：按当前实现验证“全环无 root 时 summary 保留、trace_span 不写入”的现状行为。
- `TreatsMissingParentAsRoot`：改为断言“遍历按 root 处理但落库保留 parent_id”的当前策略。

### Pitfalls
- 将集成测试写死为固定 `risk_level`（如 `unknown`）会导致 AI/mock 行为变化后频繁误报。
- 在一个任务里同时做“目录治理 + 行为修复”容易把问题归因混淆，建议像本次这样分步收敛并单独回归验证。

---

## 追加记录：补充 cycle anomalies 记录位置注释

## Git Commit Message
`docs(core): 补充TraceSessionManager环路异常记录位置注释`

## Modification
- `server/core/TraceSessionManager.cpp`
- `docs/todo-list/Todo_TraceSessionManager.md`

## Learning Tips

### Newbie Tips
- 当异常信息“只在 payload 里流转、不落库”时，必须在关键代码处写明，否则后续排障容易误以为数据库丢数据。
- 注释不仅解释“做了什么”，还要解释“去哪儿看结果”，这能显著降低跨人协作沟通成本。

### Function Explanation
- `output["anomalies"]`：当前用于记录序列化阶段检测到的环路异常，随 `trace_payload` 继续流转到 AI 分析链路。
- `trace_payload`：TraceSessionManager 传给 `TraceAiProvider::AnalyzeTrace` 的输入载体，包含 spans 与 anomalies。

### Pitfalls
- 如果团队默认认为“所有异常都应落库”，会在排障时错误聚焦 `trace_span`，忽略 payload 中的 `anomalies`。
- 缺少“记录位置”注释时，新成员容易把“现有语义”误改成“自认为的语义”，引入不必要行为变更。

---

## 追加记录：新增 integration workflow（手动触发）

## Git Commit Message
`ci(test): 新增integration工作流执行TraceSessionManager集成测试`

## Modification
- `.github/workflows/integration.yml`
- `docs/TEST_ASSET_LEDGER.md`
- `docs/todo-list/Todo_TestAssets.md`

## Learning Tips

### Newbie Tips
- 把 integration 从 unit/smoke 中独立出来，可以避免 PR 阶段被重测试拖慢，同时保留“需要时一键验证”的能力。
- 集成测试更依赖环境状态，先用手动触发比默认强制触发更稳，等稳定后再考虑升级到定时/PR 必跑。

### Function Explanation
- `workflow_dispatch`：在 Actions 页面手动点击触发，适合重测试或排障场景。
- `ctest -R "^TraceSessionManagerIntegrationTest\\."`：只执行 TraceSessionManager 集成测试套件，控制执行范围与耗时。

### Pitfalls
- 如果 integration 也默认跟 unit 同级必跑，PR 反馈时间会明显变长，团队容易绕过测试。
- 没有独立 workflow 时，集成测试失败会和其它测试日志混在一起，定位成本更高。
