# Suite D Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 Suite D 的正式实验入口，让我们能稳定复跑 `4` 核本机 `AI-on` 证明图、`24` 核云机 `AI-off` 拓扑搜索、主扩展曲线和对应 flamegraph。

**Architecture:** 不再硬改老的 `common/run_wrk_case.sh` 去承担 Suite D 全部语义，而是在 `server/tests/benchmark/suite_d/` 新增一套 Python orchestration：单 case runner 负责起停后端、跑 `wrk`、轮询 SQLite、写 `result.json`；拓扑搜索和主曲线 runner 只负责编排 candidate/load point 并聚合 `summary.json`。`wrk` 继续做主发生器，但用一份 Suite D 专用 Lua 脚本补齐 `offered traces` 与 `p95/p99` 输出，避免把 Suite A 旧脚本口径改脏。

**Tech Stack:** Python 3、argparse、unittest、bash、wrk Lua、SQLite、现有 `LogSentinel` CLI、现有 benchmark 文档体系

---

## File Map

- Create: `server/tests/benchmark/common/utils/trace_sqlite_polling.py`
  - 共享的 SQLite 计数读取与“追平/稳定”轮询 helper，供 Suite A 与 Suite D 共用。
- Create: `server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py`
  - 锁共享 SQLite 轮询 helper 的稳定语义，避免 Suite D 接进来时把 Suite A 弄回归。
- Modify: `server/tests/benchmark/suite_a/run_suite_a_case.py`
  - 切到共享 polling helper，不改 Suite A 结果语义。
- Create: `server/tests/benchmark/suite_d/trace_model_suite_d.lua`
  - Suite D 专用 wrk Lua，保持 clean end-trace，但额外打印 `offered_traces` 和 `latency_p95/p99`。
- Create: `server/tests/benchmark/suite_d/run_suite_d_case.py`
  - 单 case runner：起后端、跑 wrk、记 `t_stop`、轮询 SQLite、产出 `result.json`。
- Create: `server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
  - 锁单 case CLI、wrk 摘要解析、online completion/drain 计算和 JSON 结构。
- Create: `server/tests/benchmark/suite_d/run_suite_d_topology_search.py`
  - `24` 核 `T1/T2/T3` 小搜索 runner。
- Create: `server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py`
  - 锁 candidate 表、排序口径、stdout 摘要和 `summary.json` 结构。
- Create: `server/tests/benchmark/suite_d/run_suite_d_scaling.py`
  - 正式主曲线 runner：按总核心数 `4/8/12/16/20/24` 编排 case。
- Create: `server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py`
  - 锁总核数表、sender/backend 分配、线程/水位映射和聚合输出。
- Create: `server/tests/benchmark/suite_d/run_suite_d_local4_ai_on.sh`
  - 本机 `4` 核轻量证明图 wrapper。
- Create: `server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh`
  - `24` 核拓扑搜索 wrapper。
- Create: `server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh`
  - `24` 核主曲线 wrapper。
- Create: `server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh`
  - `24` 核、`AI-off`、Suite D Lua 口径的 flamegraph wrapper。
- Create: `server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
  - 锁 4 个 wrapper 的命令口径。
- Modify: `server/tests/benchmark/common/run_flamegraph_case.sh`
  - 补 `SERVER_IO_THREADS` 与 `DISABLE_AI/DISABLE_WEBHOOK/NO_AUTO_START_PROXY` 等环境开关，服务 Suite D flamegraph。
- Modify: `server/tests/benchmark/README.md`
- Modify: `docs/BENCHMARK_SUITE_OVERVIEW.md`
- Modify: `docs/todo-list/Todo_Benchmark.md`
- Modify: `docs/dev-log/20260417-feat-suite-a-buffer-compare.md`

---

### Task 1: 抽共享 SQLite Polling Helper

**Files:**
- Create: `server/tests/benchmark/common/utils/trace_sqlite_polling.py`
- Create: `server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_case.py`
- Test: `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`

- [ ] **Step 1: Write the failing shared-helper test**

在 `trace_sqlite_polling_unit_test.py` 锁两类行为：

```python
def test_wait_until_trace_summary_reaches_expected_count_before_declaring_stable():
    counts = iter([
        {"trace_summary": 12, "trace_span": 96},
        {"trace_summary": 12, "trace_span": 96},
        {"trace_summary": 15, "trace_span": 120},
        {"trace_summary": 15, "trace_span": 120},
        {"trace_summary": 15, "trace_span": 120},
    ])
    ticks = iter([1000, 1050, 1100, 1150, 1200, 1250])
    result = polling.wait_until_sqlite_stable(
        sqlite_path=Path("/tmp/demo.db"),
        sqlite_counter=lambda _path: next(counts),
        expected_trace_count=15,
        poll_interval_ms=50,
        stable_rounds=2,
        confirm_sleep_ms=10,
        max_wait_ms=5000,
        base_ms=1000,
        monotonic_ms=lambda: next(ticks),
    )
    assert result["final_counts"]["trace_summary"] == 15
```

```python
def test_wait_until_sqlite_stable_returns_timeout_counts_when_never_catches_up():
    counts = iter([
        {"trace_summary": 8, "trace_span": 64},
        {"trace_summary": 8, "trace_span": 64},
        {"trace_summary": 8, "trace_span": 64},
    ])
    ticks = iter([1000, 1050, 1100, 1150, 1200, 1250])
    result = polling.wait_until_sqlite_stable(
        sqlite_path=Path("/tmp/demo.db"),
        sqlite_counter=lambda _path: next(counts),
        expected_trace_count=10,
        poll_interval_ms=50,
        stable_rounds=2,
        confirm_sleep_ms=10,
        max_wait_ms=120,
        base_ms=1000,
        monotonic_ms=lambda: next(ticks),
    )
    assert result["final_counts"]["trace_summary"] == 8
    assert result["timed_out"] is True
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m unittest \
  server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py \
  server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py
```

Expected: FAIL，因为共享 helper 还不存在，Suite A 也还没切过去。

- [ ] **Step 3: Write the shared helper**

在 `trace_sqlite_polling.py` 放这几个最小公共接口：

```python
def read_sqlite_counts(sqlite_path: Path) -> JsonDict:
    sqlite_uri = f"file:{sqlite_path}?mode=ro"
    conn = sqlite3.connect(sqlite_uri, uri=True, timeout=5.0)
    try:
        return {
            "trace_summary": int(conn.execute("SELECT COUNT(*) FROM trace_summary").fetchone()[0]),
            "trace_span": int(conn.execute("SELECT COUNT(*) FROM trace_span").fetchone()[0]),
        }
    finally:
        conn.close()

def wait_until_sqlite_stable(
    sqlite_path: Path,
    *,
    sqlite_counter: Callable[[Path], JsonDict] = read_sqlite_counts,
    expected_trace_count: Optional[int],
    poll_interval_ms: int,
    stable_rounds: int,
    confirm_sleep_ms: int,
    max_wait_ms: int,
    base_ms: int,
    monotonic_ms: Callable[[], int] = monotonic_time_ms,
) -> JsonDict:
    stable_hits = 0
    last_counts: Optional[JsonDict] = None
    deadline_ms = base_ms + max_wait_ms
    while monotonic_ms() <= deadline_ms:
        counts = sqlite_counter(sqlite_path)
        if expected_trace_count is not None and int(counts["trace_summary"]) < expected_trace_count:
            stable_hits = 0
            last_counts = counts
            continue
        if counts == last_counts:
            stable_hits += 1
        else:
            stable_hits = 1
            last_counts = counts
        if stable_hits >= stable_rounds:
            return {"final_counts": counts, "drain_tail_ms": max(0, monotonic_ms() - base_ms), "timed_out": False}
    return {"final_counts": last_counts or {"trace_summary": 0, "trace_span": 0}, "drain_tail_ms": max_wait_ms, "timed_out": True}
```

要求：

- 继续沿用 Suite A 现有“先追 expected trace count，再谈稳定”的语义；
- timeout 时仍要回最新 counts；
- 返回结构保持：

```python
{
    "final_counts": {"trace_summary": 15, "trace_span": 120},
    "drain_tail_ms": 9800,
    "timed_out": False,
}
```

- [ ] **Step 4: Switch Suite A to the shared helper without changing behavior**

在 `run_suite_a_case.py` 改成：

```python
from server.tests.benchmark.common.utils.trace_sqlite_polling import (
    read_sqlite_counts,
    wait_until_sqlite_stable,
)
```

只允许删掉重复函数定义，不允许改 Suite A 对外 JSON 字段名。

- [ ] **Step 5: Run tests to verify they pass**

Run:

```bash
python3 -m unittest \
  server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py \
  server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add \
  server/tests/benchmark/common/utils/trace_sqlite_polling.py \
  server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py \
  server/tests/benchmark/suite_a/run_suite_a_case.py
git commit -m "refactor(benchmark): 抽共享 SQLite 轮询 helper"
```

---

### Task 2: 落 Suite D 单 Case Runner 与 wrk 摘要口径

**Files:**
- Create: `server/tests/benchmark/suite_d/trace_model_suite_d.lua`
- Create: `server/tests/benchmark/suite_d/run_suite_d_case.py`
- Create: `server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`

- [ ] **Step 1: Write the failing unit tests for single-case contract**

先锁住 3 件事：

1. CLI 参数

```python
args = suite_d_case.parse_args([
    "--server-bin", "./server/build/LogSentinel",
    "--run-root", "/tmp/suite_d_case",
    "--server-cpuset", "1-3",
    "--wrk-cpuset", "0",
    "--server-io-threads", "2",
    "--dispatch-worker-threads", "1",
    "--worker-threads", "12",
    "--worker-queue-size", "4096",
    "--trace-active-session-limit", "512",
    "--trace-buffered-span-limit", "4096",
    "--connections", "120",
    "--duration", "15s",
    "--warmup-duration", "3s",
    "--disable-ai",
])
assert args.disable_ai is True
```

2. wrk 摘要解析

```python
sample_output = '''
Running 15s test @ http://127.0.0.1:18080
  2 threads and 120 connections
  Latency Distribution
     95%   12.35ms
     99%   18.90ms
Requests/sec:  812.34
12184 requests in 15.00s, 1.20MB read
trace_model_suite_d metrics: offered_traces=1523 spans_per_trace=8 latency_p95_ms=12.35 latency_p99_ms=18.90
'''
metrics = suite_d_case.parse_wrk_metrics(sample_output)
assert metrics["offered_traces"] == 1523
assert metrics["requests"] == 12184
```

3. 结果 JSON 结构

```python
assert result["online_completed_traces_per_sec"] == 38.4
assert result["online_completion_ratio"] == 0.92
assert result["drain_tail_ms"] == 850
assert result["wrk_metrics"]["offered_traces"] == 1523
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m unittest server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py
```

Expected: FAIL，因为 Lua 脚本、runner 和解析函数都还不存在。

- [ ] **Step 3: Add Suite D-specific wrk Lua summary line**

在 `trace_model_suite_d.lua` 复制当前 `trace_model.lua` 的 clean end-trace骨架，只补 Suite D 需要的 summary 行：

```lua
function done(summary, latency, requests)
    local request_count = 0
    if summary ~= nil and summary.requests ~= nil then
        request_count = summary.requests
    elseif requests ~= nil and requests.total ~= nil then
        request_count = requests.total
    end
    local offered_traces = math.floor(request_count / spans_per_trace)
    local p95_ms = latency:percentile(95.0) / 1000.0
    local p99_ms = latency:percentile(99.0) / 1000.0
    io.write(string.format(
        "trace_model_suite_d metrics: offered_traces=%d spans_per_trace=%d latency_p95_ms=%.2f latency_p99_ms=%.2f\n",
        offered_traces,
        spans_per_trace,
        p95_ms,
        p99_ms
    ))
end
```

要求：

- 只服务 Suite D；
- 不要回头改 `common/wrk/trace_model.lua`；
- 继续默认 clean end-trace，不把 Suite B 脏时序语义塞进来。

- [ ] **Step 4: Write the minimal single-case runner**

`run_suite_d_case.py` 负责：

- 自动派生 `run_root / sqlite_db / server_log / result.json`
- 按显式 cpuset 起后端
- 支持 `--disable-ai --disable-webhook --no-auto-start-proxy`
- 先 warmup，再跑正式 wrk
- 记 `t_stop`
- 读 `trace_summary` stop counts
- 调共享 SQLite polling helper 等 drain
- 产出：

```python
{
    "requested_run_root": "/tmp/suite_d_case",
    "actual_run_root": "/tmp/suite_d_case-20260417-235501-120ms",
    "server_cpuset": "1-3",
    "wrk_cpuset": "0",
    "server_io_threads": 2,
    "dispatch_worker_threads": 1,
    "worker_threads": 12,
    "wrk_metrics": {
        "requests": 12184,
        "requests_per_sec": 812.34,
        "offered_traces": 1523,
        "latency_p95_ms": 12.35,
        "latency_p99_ms": 18.90,
    },
    "sqlite_counts_at_stop": {"trace_summary": 1401, "trace_span": 11208},
    "sqlite_counts_final": {"trace_summary": 1523, "trace_span": 12184},
    "online_completed_traces_per_sec": 93.4,
    "online_completion_ratio": 0.9199,
    "drain_tail_ms": 850,
    "drain_timeout": false,
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run:

```bash
python3 -m unittest \
  server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py \
  server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add \
  server/tests/benchmark/suite_d/trace_model_suite_d.lua \
  server/tests/benchmark/suite_d/run_suite_d_case.py \
  server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py
git commit -m "feat(benchmark): 增加 Suite D 单 case runner"
```

---

### Task 3: 落 `24` 核小拓扑搜索 Runner

**Files:**
- Create: `server/tests/benchmark/suite_d/run_suite_d_topology_search.py`
- Create: `server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py`
- Test: `server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`

- [ ] **Step 1: Write the failing topology-search test**

锁住这 4 件事：

1. candidate 表只有 `T1/T2/T3`

```python
assert search_module.DEFAULT_CANDIDATES == [
    {"name": "t1", "server_io_threads": 4, "dispatch_worker_threads": 3, "worker_threads": 32},
    {"name": "t2", "server_io_threads": 6, "dispatch_worker_threads": 4, "worker_threads": 32},
    {"name": "t3", "server_io_threads": 6, "dispatch_worker_threads": 5, "worker_threads": 48},
]
```

2. 排序主键

先按 `online_completed_traces_per_sec` 降序，再按 `online_completion_ratio` 降序，再按 `drain_tail_ms` 升序。

3. stdout 只打一行摘要

```text
[candidate 2/3] name=t2 online=93.40 ratio=0.9200 drain=850 qps=812.34 winner_so_far=t2
```

4. `summary.json` 里要有 `top_candidates`

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m unittest server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py
```

Expected: FAIL，因为搜索 runner 还不存在。

- [ ] **Step 3: Implement the search runner**

`run_suite_d_topology_search.py` 只做这些事：

- 固定 `24` 核资源预算；
- 固定 `sender=4`、`backend=20`；
- 固定 `AI-off + clean end-trace`；
- 调 `run_suite_d_case.run_suite_d_case(...)` 跑每个 candidate；
- 输出简洁 stdout；
- 完整明细写 `summary.json`。

要求：

- 不把主曲线的 `4/8/12/16/20/24` 档位混进这一步；
- 不在这一步扫描水位大矩阵；
- 只允许 `2~3` 组候选。

- [ ] **Step 4: Run tests to verify they pass**

Run:

```bash
python3 -m unittest \
  server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py \
  server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add \
  server/tests/benchmark/suite_d/run_suite_d_topology_search.py \
  server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py
git commit -m "feat(benchmark): 增加 Suite D 24核拓扑搜索"
```

---

### Task 4: 落主扩展曲线 Runner 与冻结 Wrapper

**Files:**
- Create: `server/tests/benchmark/suite_d/run_suite_d_scaling.py`
- Create: `server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py`
- Create: `server/tests/benchmark/suite_d/run_suite_d_local4_ai_on.sh`
- Create: `server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh`
- Create: `server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh`
- Create: `server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`
- Modify: `server/tests/benchmark/suite_d/run_suite_d.sh`

- [ ] **Step 1: Write the failing scaling/wrapper tests**

锁这几件事：

1. 主曲线总核数表

```python
assert scaling_module.DEFAULT_TOTAL_CORE_POINTS == [4, 8, 12, 16, 20, 24]
```

2. sender/backend 分配

```python
assert scaling_module.DEFAULT_CORE_SPLIT[4] == {"sender_cores": 1, "backend_cores": 3}
assert scaling_module.DEFAULT_CORE_SPLIT[24] == {"sender_cores": 4, "backend_cores": 20}
```

3. `T2` 比例缩放表

```python
assert scaling_module.DEFAULT_TOPOLOGY_MAP[16] == {
    "server_io_threads": 4,
    "dispatch_worker_threads": 3,
    "worker_threads": 24,
}
```

4. wrapper 口径

`run_suite_d_local4_ai_on.sh` 必须调用 `run_suite_d_case.py`  
`run_suite_d_topology_search_24c.sh` 必须调用 `run_suite_d_topology_search.py`  
`run_suite_d_scaling_24c.sh` 必须调用 `run_suite_d_scaling.py`

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
python3 -m unittest \
  server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py \
  server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py
```

Expected: FAIL，因为 scaling runner 和 wrapper 都还不存在。

- [ ] **Step 3: Implement the scaling runner**

`run_suite_d_scaling.py` 负责：

- 固定 `AI-off`
- 固定 `clean end-trace`
- 固定总核数点位 `4/8/12/16/20/24`
- 按表映射 sender/backend cpuset、线程数、水位
- 逐档调用 `run_suite_d_case.run_suite_d_case(...)`
- 聚合：

```python
{
    "by_total_cores": [
        {
            "total_cores": 4,
            "sender_cores": 1,
            "backend_cores": 3,
            "online_completed_traces_per_sec": 18.2,
            "online_completion_ratio": 0.81,
            "drain_tail_ms": 1400,
            "wrk_metrics": {
                "requests": 1456,
                "requests_per_sec": 97.1,
                "offered_traces": 182,
                "latency_p95_ms": 22.4,
                "latency_p99_ms": 31.7,
            },
        },
        {
            "total_cores": 24,
            "sender_cores": 4,
            "backend_cores": 20,
            "online_completed_traces_per_sec": 93.4,
            "online_completion_ratio": 0.92,
            "drain_tail_ms": 850,
            "wrk_metrics": {
                "requests": 12184,
                "requests_per_sec": 812.34,
                "offered_traces": 1523,
                "latency_p95_ms": 12.35,
                "latency_p99_ms": 18.90,
            },
        },
    ],
    "overall": {
        "best_online_completed_traces_per_sec": 93.4,
        "best_total_cores": 24,
    }
}
```

- [ ] **Step 4: Add frozen wrappers**

`run_suite_d_local4_ai_on.sh`

- 固定本机 `4` 核、`AI-on`、`1` 个代表性负载点；
- 结果只服务“轻量环境完整链路能跑”的证明图。

`run_suite_d_topology_search_24c.sh`

- 固定 `24` 核、`AI-off`、只跑 `T1/T2/T3`；
- 结果只服务选基线拓扑。

`run_suite_d_scaling_24c.sh`

- 固定 `24` 核云机主曲线；
- 统一 `AI-off`；
- 用选定基线拓扑按比例映射全部总核数档位。

同时把 `run_suite_d.sh` 改成“兼容入口”，在注释里明确它是 generic wrapper，不是论文正式命令。

- [ ] **Step 5: Run tests to verify they pass**

Run:

```bash
python3 -m unittest \
  server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py \
  server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py
bash -n \
  server/tests/benchmark/suite_d/run_suite_d_local4_ai_on.sh \
  server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh \
  server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add \
  server/tests/benchmark/suite_d/run_suite_d_scaling.py \
  server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py \
  server/tests/benchmark/suite_d/run_suite_d_local4_ai_on.sh \
  server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh \
  server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh \
  server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py \
  server/tests/benchmark/suite_d/run_suite_d.sh
git commit -m "feat(benchmark): 冻结 Suite D 主曲线入口"
```

---

### Task 5: 补 Suite D Flamegraph AI-off 口径

**Files:**
- Modify: `server/tests/benchmark/common/run_flamegraph_case.sh`
- Modify: `server/tests/benchmark/suite_d/run_flamegraph.sh`
- Create: `server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh`
- Test: `server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py`

- [ ] **Step 1: Write the failing wrapper expectation**

在 `suite_d_frozen_wrappers_unit_test.py` 增加对 flamegraph wrapper 的锁定：

- 使用 `trace_model_suite_d.lua`
- 支持 `SERVER_IO_THREADS`
- 支持 `DISABLE_AI=1`
- 支持 `DISABLE_WEBHOOK=1`
- 支持 `NO_AUTO_START_PROXY=1`

- [ ] **Step 2: Extend the common flamegraph runner with explicit Suite D knobs**

`run_flamegraph_case.sh` 新增环境变量消费：

```bash
SERVER_IO_THREADS="${SERVER_IO_THREADS:-1}"
DISABLE_AI="${DISABLE_AI:-0}"
DISABLE_WEBHOOK="${DISABLE_WEBHOOK:-0}"
NO_AUTO_START_PROXY="${NO_AUTO_START_PROXY:-0}"
```

拼 server 命令时只做最小追加：

```bash
--server-io-threads "${SERVER_IO_THREADS}"
```

以及按布尔值决定是否继续带：

```bash
--disable-ai
--disable-webhook
--no-auto-start-proxy
```

- [ ] **Step 3: Add the frozen 24-core flamegraph wrapper**

`run_suite_d_flamegraph_24c.sh` 固定：

- `AI-off`
- `clean end-trace`
- Suite D 专用 Lua
- 与主曲线同一套 `24` 核拓扑

这样 flamegraph 只是主曲线的结构解释图，不再是另一套口径。

- [ ] **Step 4: Run syntax/tests**

Run:

```bash
python3 -m unittest server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py
bash -n \
  server/tests/benchmark/common/run_flamegraph_case.sh \
  server/tests/benchmark/suite_d/run_flamegraph.sh \
  server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add \
  server/tests/benchmark/common/run_flamegraph_case.sh \
  server/tests/benchmark/suite_d/run_flamegraph.sh \
  server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh
git commit -m "feat(benchmark): 补 Suite D flamegraph AI-off 入口"
```

---

### Task 6: 文档、验证与最小 dry-run

**Files:**
- Modify: `server/tests/benchmark/README.md`
- Modify: `docs/BENCHMARK_SUITE_OVERVIEW.md`
- Modify: `docs/todo-list/Todo_Benchmark.md`
- Modify: `docs/dev-log/20260417-feat-suite-a-buffer-compare.md`

- [ ] **Step 1: Update docs to match the frozen Suite D narrative**

文档必须写清：

- `4` 核本机 `AI-on` 证明图和 `24` 核主曲线不是同一件事；
- 主曲线统一 `AI-off`；
- 主指标是 `online_completed_traces_per_sec`；
- `wrk` 仍是主发生器，但补了 `offered traces` 计数；
- `run_suite_d.sh` 是 generic wrapper，不是论文正式入口；
- 正式入口是新加的 frozen wrappers。

- [ ] **Step 2: Run focused unit tests**

Run:

```bash
python3 -m unittest \
  server/tests/benchmark/common/utils/trace_sqlite_polling_unit_test.py \
  server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py \
  server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py \
  server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py \
  server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py \
  server/tests/benchmark/suite_d/suite_d_frozen_wrappers_unit_test.py
```

Expected: PASS

- [ ] **Step 3: Run Python syntax and shell syntax checks**

Run:

```bash
python3 -m py_compile \
  server/tests/benchmark/common/utils/trace_sqlite_polling.py \
  server/tests/benchmark/suite_d/run_suite_d_case.py \
  server/tests/benchmark/suite_d/run_suite_d_topology_search.py \
  server/tests/benchmark/suite_d/run_suite_d_scaling.py

bash -n \
  server/tests/benchmark/suite_d/run_suite_d_local4_ai_on.sh \
  server/tests/benchmark/suite_d/run_suite_d_topology_search_24c.sh \
  server/tests/benchmark/suite_d/run_suite_d_scaling_24c.sh \
  server/tests/benchmark/suite_d/run_suite_d_flamegraph_24c.sh
```

Expected: PASS

- [ ] **Step 4: Run a minimal dry-run smoke**

只跑最小时间窗口，不追求正式数字，只验证链路没断：

```bash
python3 server/tests/benchmark/suite_d/run_suite_d_case.py \
  --run-root /tmp/suite_d_case_smoke \
  --server-bin ./server/build/LogSentinel \
  --server-cpuset 1-2 \
  --wrk-cpuset 0 \
  --server-io-threads 1 \
  --dispatch-worker-threads 1 \
  --worker-threads 8 \
  --worker-queue-size 4096 \
  --trace-active-session-limit 512 \
  --trace-buffered-span-limit 4096 \
  --trace-max-dispatch-per-tick 64 \
  --connections 20 \
  --wrk-threads 1 \
  --warmup-duration 1s \
  --duration 2s \
  --disable-ai \
  --disable-webhook \
  --no-auto-start-proxy
```

Expected:

- stdout 打出单行摘要；
- `/tmp/suite_d_case_smoke-*/result.json` 存在；
- 结果 JSON 至少包含 `wrk_metrics.offered_traces`、`online_completed_traces_per_sec`、`drain_tail_ms`。

- [ ] **Step 5: Run final diff hygiene**

Run:

```bash
git diff --check
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add \
  server/tests/benchmark/README.md \
  docs/BENCHMARK_SUITE_OVERVIEW.md \
  docs/todo-list/Todo_Benchmark.md \
  docs/dev-log/20260417-feat-suite-a-buffer-compare.md
git commit -m "docs(benchmark): 收口 Suite D 正式命令与口径"
```
