# 测试资产台账（2026-03-03）

## 1. 目标与范围

本台账用于先把现有测试资产“讲清楚”再做下一步动作，避免重复造轮子或误删有效资产。  
范围包含：
- `server/tests` 下 C++/Python 测试脚本
- `server/CMakeLists.txt` 中 CTest 接入情况
- `.github/workflows` 中 CI 覆盖情况

## 2. 总览结论

当前测试资产可以分三层：

1. **核心可用层（建议保留并持续维护）**
- C++ 单测/仓储测试（已接入 CTest）
- `TraceSessionManager_unit_test`（12/12 通过）
- `smoke_trace_spans.py`（basic/advanced 双模式）

2. **可用但需要优化层（建议重构，不建议直接删除）**
- `TraceSessionManager_test.cpp`（与 unit 存在职责重叠）
- `TraceSessionManager_integration_test.cpp`（覆盖有效，但依赖较重）
- CMake 中 `add_test + gtest_discover_tests` 并存，存在重复注册

3. **历史遗留层（建议下线或改造成 manual）**
- `run_tests.py`
- `test_mvp1.py`
- `test_mvp2.1_gemini.py`
- `integration_gemini_test.py`
- `test_httpserver.py`

> 说明：本次先做“台账决策”，不在这一轮直接删文件；下线动作建议分批执行，避免误伤。

## 3. 资产明细

### 3.1 C++ 测试资产（CTest）

| 资产 | 类型 | 当前状态 | 结论 | 建议动作 |
|---|---|---|---|---|
| `http_context_test.cpp` | 单测 | 已接入 CTest，稳定 | 保留 | 维持 |
| `util_traceidgenerate_test.cpp` | 单测 | 已接入 CTest，稳定 | 保留 | 维持 |
| `threadpool_test.cpp` | 单测 | 已接入 CTest，稳定 | 保留 | 维持 |
| `SqliteLogRepository_test.cpp` | 仓储测试 | 已接入 CTest | 保留 | 维持 |
| `LogBatcher_test.cpp` | 单测 | 已接入 CTest | 保留 | 维持 |
| `TraceSessionManager_unit_test.cpp` | 核心单测 | 已接入 CTest，12/12 通过 | 强保留 | 作为主防线 |
| `TraceSessionManager_test.cpp` | 早期基础测 | 与 unit 测职责重叠 | 优化 | 合并到 unit 后下线 |
| `TraceSessionManager_integration_test.cpp` | 集成测 | 覆盖广但依赖重 | 保留+优化 | 标记为 integration 分组 |
| `SqliteTraceRepository_test.cpp` | 仓储核心测试 | 已接入 CTest | 强保留 | 继续补边界 |
| `SqliteConfigRepository_test.cpp` | 仓储测试 | 已接入 CTest | 保留 | 维持 |

### 3.2 Python 测试/脚本资产

| 资产 | 类型 | 当前状态 | 结论 | 建议动作 |
|---|---|---|---|---|
| `smoke_trace_spans.py` | 冒烟测试 | 双模式，当前主冒烟入口 | 强保留 | 继续作为 smoke 主脚本 |
| `mock_webhook_server.py` | 测试辅助服务 | 被 advanced smoke 依赖 | 保留 | 维持 |
| `run_tests.py` | 旧集成入口 | 注释/流程与当前架构不一致 | 下线候选 | 重写或废弃 |
| `test_history_api.py` | API集成脚本 | 仍可用，但耦合旧入口 | 优化 | 并入统一 python test runner |
| `test_httpserver.py` | 手工验证脚本 | 多为早期路由验证脚本 | 下线候选 | 改名为 `manual_*` 或迁移 |
| `integration_gemini_test.py` | 外部依赖集成 | 强依赖环境与旧表结构假设 | 下线候选 | 重写为新 Trace 链路版 |
| `test_mvp1.py` | 历史脚本 | 依赖已不再构建的二进制 | 下线候选 | 归档/删除 |
| `test_mvp2.1_gemini.py` | 历史脚本 | 仅发送请求，断言不足 | 下线候选 | 删除或改为示例 |

## 4. CI 覆盖现状

### 已有 workflow
- `smoke.yml`
  - `smoke-basic`：PR/main 上执行
  - `smoke-advanced`：仅手动触发
- `unit.yml`
  - 当前执行：`ctest -R test_trace_session_manager_unit --output-on-failure`

### 现状评价
- 主链路已具备“单测 + 冒烟”最小闭环。
- 但 unit 覆盖仍偏窄（当前只跑 `TraceSessionManager`），其余 CTest 资产尚未纳入 CI 分层策略。

## 5. 优化与下线决策（建议）

## 5.1 立即可做（低风险）
- 统一标注测试分层：`unit / integration / smoke / manual`
- 在文档中明确每个脚本的运行入口与适用场景
- 为下线候选脚本加“DEPRECATED”头注释，先软下线一轮

## 5.2 下一步执行（中风险）
- 合并 `TraceSessionManager_test.cpp` 到 `TraceSessionManager_unit_test.cpp`
- 清理 CMake 中重复测试注册策略（保留一种可维护方案）
- 重写 `run_tests.py` 为统一 Python 测试入口（可按 `--suite` 跑）

## 5.3 最后执行（高风险/需环境）
- 重写 `integration_gemini_test.py` 以匹配当前 Trace 表结构与链路
- 清理 `test_mvp1.py`、`test_mvp2.1_gemini.py` 等历史遗留脚本

## 6. 建议的后续节奏

1. 先做“软下线”：标记弃用，不立即删除
2. 完成统一入口和命名规范后，再做真正删除
3. 每次下线前必须先确认：
- 是否仍被文档/脚本/workflow 引用
- 是否有替代测试覆盖同一风险点

---

如果你同意这份台账，我们下一步就按这里的 `5.1 -> 5.2` 节奏逐条实施，不再盲目新建测试。
