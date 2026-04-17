# Suite B 目录说明

这里预留给生命周期鲁棒性实验。

当前第一刀已经落地：

- `profiles.py`
  - 固定 `clean_baseline / mixed_realistic / late_replay_stress` 三档概率。
- `sender.py`
  - 生成 trace 模板；
  - 按 span 角色抽 delay bucket；
  - 用 min-heap 按计划时间调度；
  - 主线程统一写 `manifest.jsonl`，worker 线程只负责阻塞发送；
  - 支持 `--send-workers` 增加发送并发；
  - 输出 `manifest.jsonl` 真值账本。
- `sender_unit_test.py`
  - 覆盖 profile、延迟桶、trace 模板、min-heap 调度和 manifest 字段。
- `evaluator.py`
  - 轮询 SQLite，等待 `trace_summary / trace_span` 计数稳定；
  - 从 manifest 提炼 `ExpectedMergeSet / ExpectedIgnoreEvents / ReplayEvents`；
  - 计算 `trace_completeness_rate / trace_pollution_rate / duplicate_persistence_rate`。
- `evaluator_unit_test.py`
  - 覆盖 SQLite 稳定等待、manifest 提炼和三项主指标口径。
- `run_suite_b.py`
  - 串起 sender + evaluator；
  - 从 `manifest.jsonl` 的 HTTP 发送起止时间计算 `ingest_latency_ms`；
  - 收口单次 case 的统一 CLI；
  - 输出一份可直接留档的 JSON 结果。
- `run_suite_b_unit_test.py`
  - 覆盖 sender/evaluator 调用顺序和统一 JSON 输出。
- `run_suite_b_matrix.py`
  - 负责 `3 x 2` 矩阵的 case 级编排；
  - 为每个 case 单独起停后端、分配独立 SQLite 和结果目录；
  - 按 sender profile 汇总 `protected - minimal` 的 `ingest_p95_latency_delta`；
  - 从 `server.log` 提取 `sqlite_unique_constraint_fail_count` 诊断指标；
  - 输出一份矩阵汇总 `summary.json`。
- `run_suite_b_matrix_unit_test.py`
  - 覆盖矩阵 case 规划、后端起停顺序和汇总 JSON 结构。
- `run_suite_b_campaign.py`
  - 负责正式论文口径的多 seed 复跑；
  - 默认固定 `20260415,20260416,20260417,20260418,20260419` 五个 seed；
  - 每个 seed 单独跑一轮 `3 x 2` matrix，并按 run-level 结果聚合正确性、p95 护栏和 SQLite UNIQUE 冲突诊断指标。
- `run_suite_b_campaign_unit_test.py`
  - 覆盖 seed 列表、端口偏移、matrix 参数透传和 campaign summary 聚合结构。

当前还没做：

- AI mock server / CPU 绑核 / 资源采样联动

这条线故意不复用 `common/wrk/` 当主入口。

原因：

- Suite B 关注的是乱序、晚到、replay 的真实语义；
- 它需要 sender manifest 和 evaluator，不只是高吞吐 wrk 压流。

## 生命周期口径

Suite B 里最容易说混的是 `Collecting`、`Sealed` 和真正进入 dispatch 之后这三段。

- `Collecting`：普通 span 正在累计。每来一个新 span，都会刷新精确 `collect_deadline_ms`；时间轮只负责粗唤醒，真正 dispatch 前还会比较 `now_ms >= collect_deadline_ms`，避免 tick 量化导致早于 `--trace-idle-timeout-ms` 收口。
- `Sealed`：只由 `trace_end / capacity / token_limit / duplicate_span` 这类明确封口条件触发。`protected` 在这段窗口内还能继续吸收 late span，但 deadline 固定，不会因为 late span 继续续命。
- `Dispatching / Tombstone`：session 已经离开 manager 或已经完成 dispatch。此时 late span 不再并回原 trace；`protected` 依赖 inflight 标记和 tombstone/TIME_WAIT 防止旧 trace 被复活，`minimal` 不保留 completed tombstone。

所以 `idle timeout` 不等于“先进入 sealed grace”。它只是 `Collecting` 阶段没有继续收到 span 后的收集截止，等满配置时间后直接准备走统一的 `sweep -> dispatch queue -> dispatch worker` 主路径。

最小 dry-run 示例：

```bash
python3 server/tests/benchmark/suite_b/sender.py \
  --profile clean_baseline \
  --trace-count 2 \
  --spans-per-trace 3 \
  --dry-run \
  --send-workers 3 \
  --manifest /tmp/suite_b_sender_manifest.jsonl
```

这里的 `dry-run` 只写 manifest，不请求后端。

如果要真正发请求，需要先启动后端，然后去掉 `--dry-run`。

补充说明：

- `manifest.jsonl` 仍然是一行一个事件，但多 worker 下默认按“完成顺序”写入，不再强行按 `planned_emit_at_ms` 排序；
- 后续 evaluator 如果需要稳定排序，再按 `planned_emit_at_ms` 和 `span_id` 做离线重排；
- sender 这里故意不让 worker 线程直接写 manifest，避免把“网络阻塞并发”和“文件写入串行账本”混成一锅。

最小 evaluator 示例：

```bash
python3 server/tests/benchmark/suite_b/evaluator.py \
  --manifest /tmp/suite_b_sender_manifest.jsonl \
  --sqlite-db /tmp/suite_b.db \
  --output-json /tmp/suite_b_eval.json
```

当前 evaluator 第一刀直接输出：

- `sqlite_final_counts`
- `trace_completeness_rate`
- `trace_pollution_rate`
- `duplicate_persistence_rate`

注意：

- 这三个主指标当前先走 `manifest + SQLite`，还没有把 runtime log 冲突证据纳入最终论文表；
- manifest 的 `expected_final_action` 不是单纯按 `delay_bucket` 静态推导。sender 会先找到同一 trace 里有效的 tail/trace_end 计划到达时间，再按 protected 的 sealed grace 窗口判断 late span 到底应不应该并入最终 trace。
- `duplicate_persistence_rate` 现在按 replay clone 自身的 `span_id` 持久化次数判断。也就是说，同一 trace 里其它 late span 造成的 extra 不再连坐 replay，避免把 pollution 误算成 duplicate。

`run_suite_b.py` 会额外从 manifest 计算 `ingest_latency_ms`：

- `count`
- `min / max / avg`
- `p50 / p95 / p99`

这里的耗时口径是 sender 视角的 `/logs/spans` HTTP 请求耗时，也就是 `actual_send_done_ms - actual_send_start_ms`。

它不包含后面的 evaluator 等待、SQLite drain 稳定等待，也不包含 matrix runner 起停后端的时间。

原因很简单：`Suite B` 的性能护栏要回答的是“protected 生命周期防护会不会让入口请求明显变慢”，不能把后台 flush 或结果查询时间混进来。

`run_suite_b_matrix.py` 还会从每个 case 的 `server.log` 里提取：

- `sqlite_unique_constraint_fail_count`

它统计的是：

- `UNIQUE constraint failed: trace_summary.trace_id`

这类 SQLite 唯一键冲突在当前 case 日志里出现了多少次。

注意：

- 这是一项诊断指标，不等于最终真的重复持久化成功了多少条；
- 它表达的是“后端已经发生了重复写尝试或重复 summary 提交尝试”，很适合辅助解释为什么 `minimal` 在脏时序下危险。

最小 run_suite_b 示例：

```bash
python3 server/tests/benchmark/suite_b/run_suite_b.py \
  --sqlite-db /tmp/suite_b.db \
  --profile mixed_realistic \
  --trace-lifecycle-profile protected \
  --manifest /tmp/suite_b_manifest.jsonl \
  --output-json /tmp/suite_b_result.json
```

注意：

- `run_suite_b.py` 第一刀只负责编排单次 case，不负责起停后端；
- `--trace-lifecycle-profile` 当前只是把实验元数据收进统一结果 JSON，真正切 `protected/minimal` 仍然由你启动后端时的 CLI 决定；
- 后面如果要跑正式 `3 x 2` 矩阵，再在它外面包一层批量脚本，不要把矩阵控制逻辑反塞回单次 runner。

最小 run_suite_b_matrix 示例：

```bash
python3 server/tests/benchmark/suite_b/run_suite_b_matrix.py \
  --run-root /tmp/suite_b_matrix \
  --sender-profiles clean_baseline,mixed_realistic,late_replay_stress \
  --trace-lifecycle-profiles protected,minimal \
  --disable-ai \
  --no-auto-start-proxy
```

注意：

- `run_suite_b_matrix.py` 是 case 级矩阵 runner，每个 case 都会拿独立的 `suite_b.db / manifest / result.json / server.log`；
- `--run-root` 现在表示实验目录前缀，不是最终落盘目录。脚本每次运行都会自动追加本地时间后缀，例如 `/tmp/suite_b_matrix-20260416-094158-237ms`；
- `summary.json` 会同时记录 `requested_run_root` 和 `actual_run_root`，前者是命令里写的前缀，后者才是真正保存本轮 SQLite / manifest / result 的目录；
- 现在默认不再要求你手写 `--server-command`，可以直接用顶层 CLI 控制后端资源参数；
- 如果你已经有外部包装脚本，仍然可以继续传 `--server-command` 模板，当前支持注入 `{sqlite_db} / {trace_lifecycle_profile} / {port} / {log_path} / {case_id} / {run_dir}`；
- 如果只是验证编排逻辑是否活着，可以配 `--dry-run`，再给它一个能监听端口的最小 dummy server。

正式论文/答辩口径不要直接拿单次 matrix 当最终结果。

正式入口使用 `run_suite_b_campaign.py`：

- 默认跑 5 个固定 seed；
- 每个 seed 跑一轮完整 `3 x 2` matrix；
- `--campaign-root` 表示 campaign 目录前缀，每次运行同样会自动追加时间后缀；
- campaign summary 会输出 `aggregate.correctness_by_case`、`aggregate.ingest_p95_latency_delta_by_profile` 和 `aggregate.sqlite_unique_constraint_fail_count_by_case`；
- 正确性指标报 `mean / min / max`，入口 p95 护栏优先看 run-level `median / min / max`。

最小 campaign 示例：

```bash
python3 server/tests/benchmark/suite_b/run_suite_b_campaign.py \
  --campaign-root /tmp/suite_b_campaign \
  --disable-ai \
  --disable-webhook \
  --no-auto-start-proxy
```

新增的资源控制 CLI：

- `--server-bin`
  - 后端可执行文件，默认 `./server/build/LogSentinel`
- `--server-cpuset`
  - 只给后端进程绑核，例如 `0-1` 或 `2-13`
- `--server-io-threads`
  - 后端 Reactor / I/O 线程数，对应后端冷启动里的 `kernel_io_threads`
- `--worker-threads`
- `--dispatch-worker-threads`
- `--worker-queue-size`
- `--trace-capacity`
- `--trace-token-limit`
- `--trace-sweep-interval-ms`
- `--trace-idle-timeout-ms`
- `--trace-max-dispatch-per-tick`
- `--trace-buffered-span-limit`
- `--trace-active-session-limit`
- `--disable-ai`
- `--disable-webhook`
- `--disable-buffered-trace-repo`
- `--no-auto-start-proxy`

这批参数的目的很直接：

- 4 核本机和 16 核云机现在只需要改 CLI 数字；
- 不需要再手改一长串 `server-command` 模板；
- 每次实验命令里就能直接看见后端到底吃了多少核、多少线程。
- 如果旧进程已经占着某个 case 端口，matrix runner 现在会直接 fail fast，不再把“旧进程还活着”误判成“新 case 已启动成功”。
- 复跑同一条命令不会覆盖旧结果，也不会复用旧 SQLite；目录靠时间后缀区分，不再靠手写 `v1/v2`。

4 核本机最小 matrix 示例：

```bash
taskset -c 0 python3 server/tests/benchmark/suite_b/run_suite_b_matrix.py \
  --server-bin ./server/build/LogSentinel \
  --run-root /tmp/suite_b_matrix_local4 \
  --port-base 19380 \
  --server-cpuset 1-3 \
  --server-io-threads 2 \
  --worker-threads 8 \
  --dispatch-worker-threads 2 \
  --worker-queue-size 4096 \
  --trace-capacity 12 \
  --trace-token-limit 0 \
  --trace-sweep-interval-ms 100 \
  --trace-idle-timeout-ms 800 \
  --trace-max-dispatch-per-tick 64 \
  --trace-buffered-span-limit 4096 \
  --trace-active-session-limit 512 \
  --trace-count 10 \
  --spans-per-trace 8 \
  --send-workers 2 \
  --disable-ai \
  --disable-webhook \
  --no-auto-start-proxy \
  --sender-profiles clean_baseline,mixed_realistic,late_replay_stress \
  --trace-lifecycle-profiles protected,minimal
```

说明：

- 这个例子用外层 `taskset -c 0` 把 matrix runner / sender 压在 1 个核上，再用 `--server-cpuset 1-3` 把后端绑到剩余 3 个核；
- `--server-io-threads 2` 表示 MiniMuduo 的 Reactor/I/O 线程也跟着抬到 2，不再继续吃 fresh DB 的默认值 1；
- `--send-workers 2` 只是 sender 的发送 worker 数，不是绑核；真正绑核靠外层 `taskset`；
- 如果本机就只有 4 核，这一档足够先验证 `protected/minimal` 的语义差异有没有出来。

### 本地 4 核已跑结果索引

- 当前优先引用：`/tmp/suite_b_matrix_local4_v2/summary.json`
- 旧版初跑 artifact：`/tmp/suite_b_matrix_local4/summary.json`
- 为什么优先看 `local4_v2`：第一次本地 `4` 核结果里，`protected__clean_baseline` 的 `trace_completeness_rate` 异常掉到 `0.0`，所以后续复跑了一版修正结果；当前文档和论文口径统一引用 `local4_v2`。
- `protected__clean_baseline`：`trace_completeness_rate = 1.0`，`trace_pollution_rate = 0.0`，`duplicate_persistence_rate = 0.0`
- `protected__mixed_realistic`：`trace_completeness_rate = 1.0`，`trace_pollution_rate = 0.0`，`duplicate_persistence_rate = 0.0`
- `protected__late_replay_stress`：`trace_completeness_rate = 0.8`，`trace_pollution_rate = 0.0`，`duplicate_persistence_rate = 0.0`
- `minimal__clean_baseline`：`trace_completeness_rate = 0.6`，`trace_pollution_rate = 0.0`，`duplicate_persistence_rate = 0.0`
- `minimal__mixed_realistic`：`trace_completeness_rate = 0.1`，`trace_pollution_rate = 0.0`，`duplicate_persistence_rate = 0.0`
- `minimal__late_replay_stress`：`trace_completeness_rate = 0.2`，`trace_pollution_rate = 0.0`，`duplicate_persistence_rate = 0.0`
- 后面如果只是要查“本地 `4` 核已经跑出什么结果”，先看这段，不要因为忘了 `/tmp` 路径再重新打一遍 matrix。

16 核云机推荐 matrix 示例：

```bash
taskset -c 0-2 python3 server/tests/benchmark/suite_b/run_suite_b_matrix.py \
  --server-bin ./server/build/LogSentinel \
  --run-root /tmp/suite_b_matrix_remote16 \
  --port-base 19580 \
  --server-cpuset 3-15 \
  --server-io-threads 5 \
  --worker-threads 16 \
  --dispatch-worker-threads 4 \
  --worker-queue-size 8192 \
  --trace-capacity 12 \
  --trace-token-limit 0 \
  --trace-sweep-interval-ms 100 \
  --trace-idle-timeout-ms 800 \
  --trace-max-dispatch-per-tick 128 \
  --trace-buffered-span-limit 8192 \
  --trace-active-session-limit 2048 \
  --trace-count 10 \
  --spans-per-trace 8 \
  --send-workers 8 \
  --disable-ai \
  --disable-webhook \
  --no-auto-start-proxy \
  --sender-profiles clean_baseline,mixed_realistic,late_replay_stress \
  --trace-lifecycle-profiles protected,minimal
```

说明：

- 这里用外层 `taskset -c 0-2` 把 matrix runner / sender 限在 3 个核上，再用 `--server-cpuset 3-15` 给后端 13 个核；
- `--server-io-threads 5` 和 `worker/dispatch` 分开看：它只负责收包、连接分发和 Reactor 回调，不负责 Trace 聚合收尾；
- `worker_threads` 和 `dispatch_worker_threads` 都是后端进程参数，跟 sender 的 `--send-workers` 不是一回事；
- 如果云机不是 16 核，就只改 `--server-cpuset / --server-io-threads / --worker-threads / --dispatch-worker-threads / --send-workers` 这几项，其他 lifecycle 参数先别乱动。

正式 16 核云机 campaign 命令：

```bash
taskset -c 0-2 python3 server/tests/benchmark/suite_b/run_suite_b_campaign.py \
  --campaign-root /tmp/suite_b_campaign_remote16 \
  --seeds 20260415,20260416,20260417,20260418,20260419 \
  --port-base 19580 \
  --port-stride 20 \
  --server-bin ./server/build/LogSentinel \
  --server-cpuset 3-15 \
  --server-io-threads 5 \
  --worker-threads 16 \
  --dispatch-worker-threads 4 \
  --worker-queue-size 8192 \
  --trace-capacity 12 \
  --trace-token-limit 0 \
  --trace-sweep-interval-ms 100 \
  --trace-idle-timeout-ms 800 \
  --trace-max-dispatch-per-tick 128 \
  --trace-buffered-span-limit 8192 \
  --trace-active-session-limit 2048 \
  --trace-count 10 \
  --spans-per-trace 8 \
  --send-workers 8 \
  --disable-ai \
  --disable-webhook \
  --no-auto-start-proxy \
  --sender-profiles clean_baseline,mixed_realistic,late_replay_stress \
  --trace-lifecycle-profiles protected,minimal
```

这条命令会实际启动 `5 x 6 = 30` 个 case。
如果只是本机 smoke，不要用它硬跑；用前面的单次 matrix 命令即可。
