# Suite B 目录说明

这里预留给生命周期鲁棒性实验。

当前第一刀已经落地：

- `profiles.py`
  - 固定 `clean_baseline / mixed_realistic / late_replay_stress` 三档概率。
- `sender.py`
  - 生成 trace 模板；
  - 按 span 角色抽 delay bucket；
  - 用 min-heap 按计划时间发送；
  - 输出 `manifest.jsonl` 真值账本。
- `sender_unit_test.py`
  - 覆盖 profile、延迟桶、trace 模板、min-heap 调度和 manifest 字段。

当前还没做：

- `evaluator.py`
- `run_suite_b.py`
- 多 worker sender

这条线故意不复用 `common/wrk/` 当主入口。

原因：

- Suite B 关注的是乱序、晚到、replay 的真实语义；
- 它需要 sender manifest 和 evaluator，不只是高吞吐 wrk 压流。

最小 dry-run 示例：

```bash
python3 server/tests/benchmark/suite_b/sender.py \
  --profile clean_baseline \
  --trace-count 2 \
  --spans-per-trace 3 \
  --dry-run \
  --manifest /tmp/suite_b_sender_manifest.jsonl
```

这里的 `dry-run` 只写 manifest，不请求后端。

如果要真正发请求，需要先启动后端，然后去掉 `--dry-run`。
