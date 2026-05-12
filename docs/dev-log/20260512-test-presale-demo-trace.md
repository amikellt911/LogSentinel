# 2026-05-12 test(presale-demo): 调整预售演示 Prompt 与规则快照证据

## Git Commit Message

`test(presale-demo): 调整预售演示 Prompt 与规则快照证据`

## Modification

- `server/tests/post_presale_demo_trace.py`
- `server/tests/import_presale_demo_prompt.py`
- `docs/DEMO_PRESALE_TRACE_CASE.md`
- `docs/todo-list/Todo_TestAssets.md`

## Learning Tips

### Newbie Tips

- 演示数据里的 `attributes` 应该记录业务系统真实可能上报的事实，例如 `config_snapshot_version`、`service_instance`、`quote_id` 和金额字段；不要写 `version_mismatch=true`、`root_cause=...` 这类结论字段，否则模型不是在分析，而是在复述答案。
- 业务 Prompt 可以告诉模型“要检查哪些证据”，但不要提前告诉它“这次一定是灰度发布或滚动更新导致”。部署、配置同步、实例状态变化都应该是基于 Trace 证据推出来的候选解释。

### Function Explanation

- `json.dumps(..., ensure_ascii=False)`：把结构化 Prompt 保存成前端可以回填的 JSON 字符串，同时保留中文可读性。
- `py_compile`：只检查 Python 文件能否通过语法编译，不会真正执行 HTTP 请求，适合这类脚本修改后的最小安全检查。

### Pitfalls

- 只给 `calculate_final_pay_amount` 一条 Span 写 `config_snapshot_version`，模型容易把版本差异当作孤立线索，优先输出“同源优惠重复抵扣”，而不是进一步联系到跨服务规则快照不一致。
- 如果 Prompt 里反复出现“灰度发布、滚动更新、配置中心延迟”，模型会把这些当作已知前提，演示时容易被质疑成“提示词泄题”。

---

# 2026-05-12 test(presale-demo): 增强预售演示脚本的自动 trace_key 与 AI 耗时统计

## Git Commit Message

`test(presale-demo): 增强预售演示脚本的 AI 耗时统计`

## Modification

- `server/tests/post_presale_demo_trace.py`
- `docs/todo-list/Todo_TestAssets.md`

## Learning Tips

### Newbie Tips

- 稳定性测试不能反复使用同一个 `trace_key`，否则新旧 trace 会在存储和查询里混在一起；默认自动生成 ID 可以避免每次测试前手工删库。
- 这里统计的耗时不是单纯模型 API 耗时，而是从开始发送 Span 到详情接口看到 AI 终态的端到端耗时，更接近用户实际等待时间。

### Function Explanation

- `argparse.BooleanOptionalAction`：可以自动生成 `--wait-ai` 和 `--no-wait-ai` 两个互斥开关，适合演示脚本这种默认等待、但偶尔需要跳过等待的场景。
- `time.monotonic()`：适合统计耗时，不受系统时间调整影响；不要用 `time.time()` 做持续时间计算。

### Pitfalls

- 轮询接口要接受 404/503 作为临时状态，因为 Trace 可能还没落库或查询线程池短暂不可用；只有其它 HTTP 错误才应该直接失败。
- AI 失败也是终态。脚本不能只等 `completed`，否则 provider 超时或熔断时会一直等到总超时，反而看不出真实失败耗时。

---

# 2026-05-12 test(presale-demo): 在演示脚本输出 AI Provider 元数据

## Git Commit Message

`test(presale-demo): 输出演示脚本的 AI Provider 元数据`

## Modification

- `server/tests/post_presale_demo_trace.py`
- `docs/todo-list/Todo_TestAssets.md`

## Learning Tips

### Newbie Tips

- 稳定性测试记录里必须写清楚 provider 和 model，否则后面看到 40 秒、50 秒的耗时，无法判断到底是哪一家模型服务导致的。
- provider/model 只能作为脚本输出元数据，不要塞进业务 Span attributes，否则 AI 会把“测试环境配置”误当成业务链路证据。

### Function Explanation

- `/api/settings/all`：同源入口下读取 Settings 快照，脚本用它拿当前 `ai_provider` 和 provider profile 的模型名。

### Pitfalls

- 如果 Settings 查询失败，脚本不能直接中断发 trace；模型稳定性测试的主目标是发 trace 和等待 AI 结果，所以这里降级打印 `unknown` 更合适。
