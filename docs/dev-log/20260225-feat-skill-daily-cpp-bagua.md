# 20260225 feat-skill-daily-cpp-bagua

## Git Commit Message
`feat(skill): 新增每日随机C++后端校招八股教学技能`

## Modification
- `docs/skills/daily-cpp-backend-bagua/SKILL.md`
- `docs/skills/daily-cpp-backend-bagua/agents/openai.yaml`
- `docs/skills/daily-cpp-backend-bagua/references/bagua-topic-pool.md`
- `docs/todo-list/Todo_DailyCppBaguaSkill.md`

## Learning Tips

### Newbie Tips
- 八股学习不要并行展开多个题目，一次吃透一个点更容易形成可复述的长期记忆。
- 面试回答不要只背结论，要同时准备“为什么成立”和“什么情况下不成立”。

### Function Explanation
- `init_skill.py`：创建标准技能目录、`SKILL.md` 模板与 `agents/openai.yaml` 基础结构。
- `generate_openai_yaml.py`：生成技能 UI 元数据，并校验 `short_description` 等约束。
- `quick_validate.py`：校验技能 frontmatter 和目录结构，确保技能可被识别与加载。

### Pitfalls
- 若缺少“没懂后的降复杂度分支”，模型容易重复原解释，学习效率会持续下降。
- 若没有“复述通过后再微坑点”的流程，容易出现熟悉感陷阱：看起来懂但不能稳定表达。
- 若不限制“一次只讲一个”，输出会退化成刷题清单，用户难以形成真正掌握。

---

## 追加记录：Trace 持久化接口命名重构（Single/Atomic）

## Git Commit Message
`refactor(persistence): 重命名Trace保存接口以明确Single与Atomic语义`

## Modification
- `server/persistence/TraceRepository.h`
- `server/persistence/SqliteTraceRepository.h`
- `server/persistence/SqliteTraceRepository.cpp`
- `server/core/TraceSessionManager.cpp`
- `server/tests/SqliteTraceRepository_test.cpp`
- `docs/todo-list/Todo_TraceRepository.md`

## Learning Tips

### Newbie Tips
- 接口命名优先表达“语义边界”，`Single` 描述输入粒度，`Atomic` 描述事务保证，两个词不要互相替代。
- 批量写库方案尚未落地时，不要提前用 `Batch` 命名现有单条写入接口，否则后续会产生认知和实现偏差。

### Function Explanation
- `SaveSingleTraceAtomic(...)`：一次调用内完成 `trace_summary/trace_span/trace_analysis/prompt_debug` 的同事务写入，任一步失败即回滚。
- `checkSqliteError(...)`：在 SQLite 返回非 `OK/DONE/ROW` 时抛异常，把错误路径统一收敛到 `catch + rollback`。

### Pitfalls
- 只改实现不改抽象接口，会导致调用方仍沿用旧语义，重构价值被抵消。
- 只改函数名不改测试名，会在排查失败时继续出现“Batch=批量写库”的误导。
- 集成测试依赖外部 AI Proxy，若本地未启动会出现与本次重命名无关的失败，需要和单元测试结果分开解读。

---

## 追加记录：补齐 TraceRepository 四个单表写入接口实现

## Git Commit Message
`feat(persistence): 完成Trace单表写入接口并补充回归测试`

## Modification
- `server/persistence/SqliteTraceRepository.cpp`
- `server/tests/SqliteTraceRepository_test.cpp`
- `docs/todo-list/Todo_TraceRepository.md`

## Learning Tips

### Newbie Tips
- `bool` 返回风格的仓储接口，建议统一“内部抛异常，边界层 catch 后返回 false”，这样调用方处理路径更稳定。
- 涉及多行写入（如 spans）即使是“单条 trace”也应使用事务，否则中途失败会造成部分落库。

### Function Explanation
- `sqlite3_prepare_v2`：预编译 SQL 语句，配合参数绑定可避免字符串拼接导致的 SQL 注入风险。
- `sqlite3_reset + sqlite3_clear_bindings`：复用同一个 statement 时必须先重置执行状态和旧参数，避免上一轮绑定值污染下一轮。
- `ROLLBACK`：事务失败时撤销已执行写入，保证接口语义与数据库状态一致。

### Pitfalls
- `SaveSingleTraceSpans` 若忽略入参 `trace_id` 与 `span.trace_id` 冲突，会写出同批次跨 trace 的脏数据。
- `analysis/prompt_debug` 依赖 `trace_summary` 外键，先写子表会失败；这类失败应作为可预期业务失败返回 `false`。
- 只实现功能不补失败场景测试，后续重构很容易把回滚/外键约束悄悄破坏。
