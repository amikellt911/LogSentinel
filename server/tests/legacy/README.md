# legacy 测试脚本说明

本目录存放历史测试脚本（legacy）。

设计目的：
- 这些脚本仍有参考价值，但已经不适合作为当前默认回归入口。
- 先集中归档到 `legacy/`，可以减少误用，同时保留历史上下文，后续再按计划重写或下线。

当前状态：
- 不纳入默认 CI。
- 仅用于历史排查或重构对照。

建议替代入口：
- Trace 冒烟测试：`python server/tests/smoke_trace_spans.py --mode basic|advanced`
- Trace 核心单测：`cd server/build && ctest -R "^TraceSessionManagerUnitTest\." --output-on-failure`

目录内脚本（软下线中）：
- `run_tests.py`
- `test_httpserver.py`
- `integration_gemini_test.py`
- `test_mvp1.py`
- `test_mvp2.1_gemini.py`
- `TraceSessionManager_test.cpp`
