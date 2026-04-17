# Benchmark Metadata Assets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 benchmark 主输出文件直接变成可复现实验资产，让 Suite A/B/D 和 flamegraph 的 JSON 自带机器信息、CPU 分配、线程拓扑、有效实验参数与产物路径。

**Architecture:** 新增一个共享 Python metadata helper，专门负责运行时系统信息采集、cpuset 解析和主 JSON 注入。各 suite runner 只提供本轮实验自己的 workload/effective flags/artifacts，再由 helper 统一拼成 `experiment_context` 和 `artifacts`，避免每个 runner 各写一套口径。

**Tech Stack:** Python 3、现有 benchmark runners、Linux `hostname/uname/lscpu` 命令、shell flamegraph wrapper

---

### Task 1: 共享 metadata helper

**Files:**
- Create: `server/tests/benchmark/common/utils/benchmark_metadata.py`
- Create: `server/tests/benchmark/common/utils/benchmark_metadata_unit_test.py`

- [ ] **Step 1: 写 helper 红灯单测**

```python
def test_collect_machine_info_uses_runtime_commands() -> None:
    ...

def test_parse_cpuset_counts_ranges_and_lists() -> None:
    ...

def test_attach_metadata_keeps_original_payload() -> None:
    ...
```

- [ ] **Step 2: 跑单测确认红灯**

Run: `python3 -m unittest server.tests.benchmark.common.utils.benchmark_metadata_unit_test`
Expected: FAIL，提示 helper 尚未实现或结构字段缺失

- [ ] **Step 3: 写最小实现**

```python
def collect_machine_info(...): ...
def parse_cpuset(...): ...
def build_cpu_allocation(...): ...
def attach_benchmark_metadata(...): ...
```

- [ ] **Step 4: 重新跑 helper 单测确认转绿**

Run: `python3 -m unittest server.tests.benchmark.common.utils.benchmark_metadata_unit_test`
Expected: PASS

### Task 2: Suite A/B/D 主 JSON 注入

**Files:**
- Modify: `server/tests/benchmark/suite_a/run_suite_a_case.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_scan.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_search_stage1.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_buffer_compare.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp.py`
- Modify: `server/tests/benchmark/suite_b/run_suite_b.py`
- Modify: `server/tests/benchmark/suite_b/run_suite_b_matrix.py`
- Modify: `server/tests/benchmark/suite_b/run_suite_b_campaign.py`
- Modify: `server/tests/benchmark/suite_d/run_suite_d_case.py`
- Modify: `server/tests/benchmark/suite_d/run_suite_d_topology_search.py`
- Modify: `server/tests/benchmark/suite_d/run_suite_d_scaling.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_scan_unit_test.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_buffer_compare_unit_test.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_main_vs_cmp_unit_test.py`
- Modify: `server/tests/benchmark/suite_b/run_suite_b_unit_test.py`
- Modify: `server/tests/benchmark/suite_b/run_suite_b_matrix_unit_test.py`
- Modify: `server/tests/benchmark/suite_b/run_suite_b_campaign_unit_test.py`
- Modify: `server/tests/benchmark/suite_d/run_suite_d_case_unit_test.py`
- Modify: `server/tests/benchmark/suite_d/run_suite_d_topology_search_unit_test.py`
- Modify: `server/tests/benchmark/suite_d/run_suite_d_scaling_unit_test.py`

- [ ] **Step 1: 先补代表性 runner 红灯断言**

```python
self.assertIn("experiment_context", result)
self.assertIn("artifacts", result)
```

- [ ] **Step 2: 跑相关 runner 单测确认红灯**

Run: `python3 -m unittest ...`
Expected: FAIL，提示缺少 metadata 字段

- [ ] **Step 3: 在各 runner 注入统一 metadata**

```python
result = attach_benchmark_metadata(
    payload=result,
    suite_name="suite_a",
    entry_script=__file__,
    ...
)
```

- [ ] **Step 4: 重新跑 suite runner 单测确认转绿**

Run: `python3 -m unittest ...`
Expected: PASS

### Task 3: Flamegraph 结构化摘要

**Files:**
- Modify: `server/tests/benchmark/common/run_flamegraph_case.sh`
- Create: `server/tests/benchmark/common/utils/write_flamegraph_summary.py`

- [ ] **Step 1: 先补 flamegraph JSON 生成入口**

```bash
python3 server/tests/benchmark/common/utils/write_flamegraph_summary.py ...
```

- [ ] **Step 2: 实现 summary writer，复用同一套 metadata helper**

```python
summary = attach_benchmark_metadata(...)
```

- [ ] **Step 3: 在 shell runner 里落 `run-summary.json`**

```bash
RUN_SUMMARY_JSON="${RUN_DIR}/run-summary.json"
```

- [ ] **Step 4: 做 shell 语法检查和最小 dry-run**

Run: `bash -n server/tests/benchmark/common/run_flamegraph_case.sh`
Expected: PASS

### Task 4: 文档、dev-log 与最终验证

**Files:**
- Modify: `server/tests/benchmark/README.md`
- Modify: `docs/BENCHMARK_SUITE_OVERVIEW.md`
- Modify: `docs/dev-log/20260417-feat-suite-a-buffer-compare.md`

- [ ] **Step 1: 更新文档，写清主 JSON 现在自带实验资产**

```text
result.json / summary.json 直接包含 experiment_context / artifacts
```

- [ ] **Step 2: 续写同日 dev-log，记录新增命令和注意事项**

```text
feat(benchmark): 为基准结果补充实验上下文资产
```

- [ ] **Step 3: 跑最终验证**

Run: `python3 -m unittest ... && python3 -m py_compile ... && bash -n server/tests/benchmark/common/run_flamegraph_case.sh && git diff --check`
Expected: 全部通过
