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
