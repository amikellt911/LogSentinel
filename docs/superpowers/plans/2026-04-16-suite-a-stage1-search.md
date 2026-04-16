# Suite A Stage 1 Search Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Suite A 补 buffer 参数 CLI 和 4 核 Stage 1 粗搜脚本，在 30 分钟预算内找到可解释的 top-k 候选参数组。

**Architecture:** 先让后端暴露 benchmark-only 的 primary flush 参数，再在 Python 侧复用 `run_suite_a_case.py` 做阶段化搜索编排。搜索脚本只输出简洁摘要，完整明细落 JSON，最后再对 top-k 做少量 AI-on smoke。

**Tech Stack:** C++17、Python 3、argparse、unittest、现有 Suite A runner

---

### Task 1: 细化 Todo 与测试基座

**Files:**
- Modify: `docs/todo-list/Todo_Benchmark.md`
- Test: `server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`

- [ ] 写 Todo 细化步骤，拆成 CLI、搜索脚本、验证三块
- [ ] 为 Stage 1 搜索脚本补红灯测试文件
- [ ] 运行新增单测，确认因脚本不存在而红灯

### Task 2: 暴露 buffer 参数 CLI

**Files:**
- Modify: `server/src/main.cpp`
- Modify: `server/persistence/BufferedTraceRepository.h`
- Modify: `server/persistence/BufferedTraceRepository.cpp`

- [ ] 在 `main.cpp` 解析 benchmark-only 的 `primary flush span threshold / interval` CLI
- [ ] 把配置透传到 `BufferedTraceRepository::Config`
- [ ] 补中文注释，说明这两个参数只服务 benchmark，不改变产品层设置语义
- [ ] 构建后端，确认编译通过

### Task 3: 实现 Stage 1 搜索脚本

**Files:**
- Create: `server/tests/benchmark/suite_a/run_suite_a_search_stage1.py`
- Modify: `server/tests/benchmark/suite_a/run_suite_a_case.py`
- Test: `server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`

- [ ] 先写红灯测试，锁 Phase A / Phase B / AI-on smoke 的 case 编排顺序
- [ ] 先写红灯测试，锁 stdout 只输出一行摘要和最终 top-k
- [ ] 实现搜索脚本，复用 `run_suite_a_case.py` 跑单 case
- [ ] 如有必要，在 `run_suite_a_case.py` 补最小公共 helper，不改原有语义
- [ ] 回跑单测，确认变绿

### Task 4: 文档与 wrapper 说明

**Files:**
- Modify: `server/tests/benchmark/README.md`
- Modify: `docs/BENCHMARK_SUITE_OVERVIEW.md`
- Modify: `docs/dev-log/20260416-fix-trace-timeout.md`

- [ ] 记录 Stage 1 搜索入口、目标和输出约束
- [ ] 补中文说明：为什么不做全排列暴力搜索，而是阶段化剪枝

### Task 5: 验证

**Files:**
- Test: `server/tests/benchmark/suite_a/run_suite_a_search_stage1_unit_test.py`
- Test: `server/tests/benchmark/suite_a/run_suite_a_case_unit_test.py`

- [ ] 运行 Suite A Python 单测
- [ ] 运行 Python 语法检查
- [ ] 构建后端确认 CLI 接线无编译错误
- [ ] `git diff --check`
