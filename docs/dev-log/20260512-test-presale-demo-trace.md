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
