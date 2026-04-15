#!/usr/bin/env bash

set -euo pipefail

# 这个 wrapper 把通用 wrk runner 的产物落到 Suite D。
# 这样 D 扫资源轴时和 A 共用同一套实现，但结果目录不会重新混在一起。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
export BENCH_SUITE="suite_d"

exec "${ROOT_DIR}/server/tests/benchmark/common/run_wrk_case.sh" "$@"
