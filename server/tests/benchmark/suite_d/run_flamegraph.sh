#!/usr/bin/env bash

set -euo pipefail

# Suite D 也复用 common flamegraph runner。
# wrapper 的职责只有一个：把结果稳定落到 suite_d，避免和 Suite A 的火焰图样本串目录。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
export BENCH_SUITE="suite_d"

exec "${ROOT_DIR}/server/tests/benchmark/common/run_flamegraph_case.sh" "$@"
