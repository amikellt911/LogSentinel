#!/usr/bin/env bash

set -euo pipefail

# 这是给历史 flamegraph 编排留下来的 generic wrapper。
# 它不是论文正式命令；正式的 24 核 AI-off 解释图应走 run_suite_d_flamegraph_24c.sh。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
export BENCH_SUITE="suite_d"

exec "${ROOT_DIR}/server/tests/benchmark/common/run_flamegraph_case.sh" "$@"
