#!/usr/bin/env bash

set -euo pipefail

# Suite A 的火焰图入口只负责打上 suite 标签。
# 具体 perf/wrk 编排放在 common runner，保证火焰图逻辑不会在 A/D 之间产生两套实现。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
export BENCH_SUITE="suite_a"

exec "${ROOT_DIR}/server/tests/benchmark/common/run_flamegraph_case.sh" "$@"
