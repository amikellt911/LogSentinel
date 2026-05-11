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

# 追加记录：2026-05-11 预售尾款业务 Prompt 导入脚本

## Git Commit Message

 test(settings): 增加预售尾款 prompt 导入脚本

## Modification

- `server/tests/import_presale_demo_prompt.py`

## Learning Tips

### Newbie Tips

导入 Prompt 不要直接改 SQLite。Settings 的真实语义不只是“表里有一行数据”，还包括 Repository 事务、快照刷新、字段兼容和前端回填格式。脚本走 `/api/settings/*`，才能和用户在页面上保存保持同一条契约。

Prompt 列表和 `active_prompt_id` 是两条接口。只写 `/settings/prompts` 只能证明 Prompt 存在，不能保证后端冷启动时会选择它作为生效 Prompt。

### Function Explanation

`GET /api/settings/all` 用于读取当前 Prompt 列表，避免导入脚本覆盖掉用户已有的其他 Prompt。

`POST /api/settings/prompts` 按当前后端契约提交完整 Prompt 列表；同名 Prompt 存在时更新，不存在时追加 `id=0` 让后端分配新 ID。

`POST /api/settings/config` 用于写入 `active_prompt_id`，让后端下一次冷启动时选择导入的预售尾款业务 Prompt。

### Pitfalls

导入后必须重启 LogSentinel。当前 `prompts + active_prompt_id + ai_language` 属于冷启动消费链，运行中的 Trace AI 不会因为这个脚本立刻换 Prompt。

脚本生成的是 SettingsPrototype 结构化 Prompt JSON，不是最终渲染后的 business_guidance 文本。这样前端五段表单才能正常回填，后续也能继续人工编辑。

## 中文注释

- 在 `build_prompt_content()` 中补充中文注释，说明脚本按前端结构化 Prompt JSON 生成内容，而不是直接写最终 business_guidance 文本。
- 在提交 `/api/settings/prompts` 前补充中文注释，说明为什么走 Settings HTTP 接口，不直改 SQLite。
- 在提交 `active_prompt_id` 前补充中文注释，说明 Prompt 列表和生效 Prompt 是两条接口，只写列表不等于生效。

## Verification

- `python3 -m py_compile server/tests/import_presale_demo_prompt.py`：通过。
- `python3 server/tests/import_presale_demo_prompt.py --help`：通过。
- 未执行真实导入，因为当前会话没有确认 LogSentinel 后端已启动。
