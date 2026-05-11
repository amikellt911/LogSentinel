# 2026-05-11 预售尾款演示 Trace 脚本

## Git Commit Message

test(trace): 增加预售尾款演示 trace 造数脚本

## Modification

- `server/tests/post_presale_demo_trace.py`

## Learning Tips

### Newbie Tips

验收演示脚本不要承担配置系统的职责。业务 Prompt、模型服务和飞书 Webhook 很多属于保存后重启生效的配置，如果脚本一边改配置一边发 Trace，现场变量会变多，失败时也很难定位。

演示脚本也不要故意制造乱序。乱序、晚到和 tombstone 是 benchmark / 论文实验要证明的机制，主演示脚本要优先保证稳定复现。

### Function Explanation

`urllib.request.Request` 用来构造标准库 HTTP POST 请求，避免为了一个演示脚本额外引入 `requests` 依赖。

`json.dumps(..., ensure_ascii=False)` 用来保留中文或业务字段原文；这里大多是英文 key，但保持这个写法可以避免后续加中文业务说明时被转义得不可读。

`argparse.ArgumentParser` 用来提供 `--base-url`、`--trace-key`、`--timeout-sec` 三个参数，方便验收时按实际端口或固定 trace_id 调整。

### Pitfalls

不要把 `demo_case`、`version_mismatch`、`same_source_violation`、`expected_amount` 等结论字段写进 attributes。attributes 应该是业务侧埋点事实，AI 的根因结论应该由 Trace 上下文和业务 Prompt 推出来。

`trace_end=true` 的 Span 不要第一个发送。脚本按“先子 Span、后父 Span、最后 root trace_end”的顺序发送，是为了避免 root 过早触发 sealed 状态，减少演示现场的不确定性。

## 中文注释

- 在 `build_presale_trace()` 里补充中文注释，说明 attributes 只放事实字段，不放会泄露答案的结论字段。
- 在返回 Span 列表前补充中文注释，说明为什么脚本按“先子后父、最后 trace_end”的稳定顺序发送。
- 在 `main()` 发送循环前补充中文注释，说明脚本只负责造数，不修改 Settings，避免现场引入冷启动配置变量。

## Verification

- `python3 -m py_compile server/tests/post_presale_demo_trace.py`：通过。
- `python3 server/tests/post_presale_demo_trace.py --help`：通过。
- 未执行真实 POST，因为当前会话没有确认后端、AI proxy、业务 Prompt 和 Webhook 已按演示配置启动。
