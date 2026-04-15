#!/usr/bin/env bash

set -euo pipefail

# 这个 wrapper 只做一件事：把通用 wrk runner 绑定到 Suite A 的结果目录。
# 真正的启动、压测和统计逻辑都放在 common 里，避免 A/D 各自拷一份脚本后越改越分叉。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
export BENCH_SUITE="suite_a"

exec "${ROOT_DIR}/server/tests/benchmark/common/run_wrk_case.sh" "$@"
