#!/usr/bin/env python3

from __future__ import annotations

import copy
import datetime as dt
import subprocess
from functools import lru_cache
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, Mapping, Optional, Sequence, Set


JsonDict = Dict[str, Any]
CommandRunner = Callable[[Sequence[str]], str]
MACHINE_INFO_COMMANDS = ("hostname", "uname -a", "lscpu")


def _default_command_runner(command: Sequence[str]) -> str:
    completed = subprocess.run(
        list(command),
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout


def _stringify_mapping(values: Optional[Mapping[str, Any]]) -> JsonDict:
    result: JsonDict = {}
    if not values:
        return result
    for key, value in values.items():
        if value is None:
            continue
        if isinstance(value, Path):
            result[str(key)] = str(value)
            continue
        result[str(key)] = value
    return result


def _parse_lscpu_output(raw_lscpu: str) -> Dict[str, str]:
    parsed: Dict[str, str] = {}
    for raw_line in raw_lscpu.splitlines():
        if ":" not in raw_line:
            continue
        key, value = raw_line.split(":", 1)
        parsed[key.strip()] = value.strip()
    return parsed


def _safe_int(value: Optional[str], default: int = 0) -> int:
    if value is None:
        return default
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return default


def _run_or_empty(command_runner: CommandRunner, command: Sequence[str]) -> str:
    try:
        return command_runner(command)
    except Exception:
        return ""


def _extract_arch_from_uname(raw_uname: str) -> str:
    parts = raw_uname.strip().split()
    if len(parts) >= 2:
        return parts[-2]
    return ""


def _extract_kernel_from_uname(raw_uname: str) -> str:
    parts = raw_uname.strip().split()
    if len(parts) >= 3:
        return parts[2]
    return raw_uname.strip()


def _extract_cpu_model_from_cpuinfo(raw_cpuinfo: str) -> str:
    for line in raw_cpuinfo.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        if key.strip().lower() == "model name":
            return value.strip()
    return ""


def _compute_machine_info(command_runner: CommandRunner) -> JsonDict:
    raw_hostname = _run_or_empty(command_runner, ("hostname",))
    raw_uname = _run_or_empty(command_runner, ("uname", "-a"))
    raw_lscpu = _run_or_empty(command_runner, ("lscpu",))
    lscpu_values = _parse_lscpu_output(raw_lscpu)

    # 机器信息这里坚持走运行时系统命令，不去手写机器备注。
    # benchmark 资产最终要跨机器搬运，只有把真实 hostname/uname/lscpu 一起落盘，
    # 后面复盘时才知道这份结果到底是哪台机子、哪套 CPU 拓扑、哪次内核环境跑出来的。
    hostname = raw_hostname.strip()
    kernel = _extract_kernel_from_uname(raw_uname)
    arch = lscpu_values.get("Architecture") or _extract_arch_from_uname(raw_uname)
    cpu_model = lscpu_values.get("Model name", "")
    logical_cpus_total = _safe_int(lscpu_values.get("CPU(s)"))
    sockets = _safe_int(lscpu_values.get("Socket(s)"))
    cores_per_socket = _safe_int(lscpu_values.get("Core(s) per socket"))
    threads_per_core = _safe_int(lscpu_values.get("Thread(s) per core"))

    # lscpu 在精简容器或裁剪环境里不一定存在。
    # 这里的回退链固定成 nproc + /proc/cpuinfo，
    # 至少把总逻辑核数和 CPU 型号补齐，不让实验资产因为缺 lscpu 直接变半残。
    if logical_cpus_total <= 0:
        raw_nproc = _run_or_empty(command_runner, ("nproc", "--all"))
        logical_cpus_total = _safe_int(raw_nproc)

    if not cpu_model:
        raw_cpuinfo = _run_or_empty(command_runner, ("cat", "/proc/cpuinfo"))
        cpu_model = _extract_cpu_model_from_cpuinfo(raw_cpuinfo)

    return {
        "hostname": hostname,
        "kernel": kernel,
        "arch": arch,
        "cpu_model": cpu_model,
        "logical_cpus_total": logical_cpus_total,
        "sockets": sockets,
        "cores_per_socket": cores_per_socket,
        "threads_per_core": threads_per_core,
        "raw_hostname": raw_hostname,
        "raw_uname": raw_uname,
        "raw_lscpu": raw_lscpu,
    }


@lru_cache(maxsize=1)
def _cached_machine_info() -> tuple[tuple[str, Any], ...]:
    info = _compute_machine_info(_default_command_runner)
    return tuple(info.items())


def collect_machine_info(
    command_runner: Optional[CommandRunner] = None,
    use_cache: bool = True,
) -> JsonDict:
    if command_runner is None:
        if use_cache:
            return dict(_cached_machine_info())
        return _compute_machine_info(_default_command_runner)
    return _compute_machine_info(command_runner)


def parse_cpuset(cpuset: Optional[str]) -> Set[int]:
    parsed: Set[int] = set()
    if cpuset is None:
        return parsed
    normalized = str(cpuset).strip()
    if not normalized:
        return parsed

    # cpuset 口径必须支持 benchmark wrapper 里常见的 `0-3,8,10-11` 写法。
    # 这里直接转成整数集合，后面统计每类进程占了多少核、总共覆盖了多少核时就不会重复算。
    for item in normalized.split(","):
        token = item.strip()
        if not token:
            continue
        if "-" in token:
            start_text, end_text = token.split("-", 1)
            start = int(start_text)
            end = int(end_text)
            if end < start:
                raise ValueError(f"invalid cpuset range: {token}")
            parsed.update(range(start, end + 1))
            continue
        parsed.add(int(token))
    return parsed


def count_cpuset_cores(cpuset: Optional[str]) -> int:
    return len(parse_cpuset(cpuset))


def build_cpu_allocation(
    server_cpuset: Optional[str] = None,
    wrk_cpuset: Optional[str] = None,
    ai_proxy_cpuset: Optional[str] = None,
) -> JsonDict:
    server_cores = parse_cpuset(server_cpuset)
    wrk_cores = parse_cpuset(wrk_cpuset)
    ai_proxy_cores = parse_cpuset(ai_proxy_cpuset)
    total_cores = server_cores | wrk_cores | ai_proxy_cores
    return {
        "server_cpuset": server_cpuset or None,
        "wrk_cpuset": wrk_cpuset or None,
        "ai_proxy_cpuset": ai_proxy_cpuset or None,
        "server_cores_used": len(server_cores),
        "wrk_cores_used": len(wrk_cores),
        "ai_proxy_cores_used": len(ai_proxy_cores),
        "total_cores_used": len(total_cores),
    }


def current_timestamp() -> str:
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")


def build_experiment_context(
    suite_name: str,
    entry_script: str,
    workload: Optional[Mapping[str, Any]] = None,
    cpu_allocation: Optional[Mapping[str, Any]] = None,
    thread_topology: Optional[Mapping[str, Any]] = None,
    effective_flags: Optional[Mapping[str, Any]] = None,
    commands: Optional[Mapping[str, Any]] = None,
    machine_info: Optional[Mapping[str, Any]] = None,
    captured_at: Optional[str] = None,
) -> JsonDict:
    context = {
        "suite": suite_name,
        "entry_script": str(Path(entry_script).resolve()),
        "captured_at": captured_at or current_timestamp(),
        "machine": _stringify_mapping(machine_info or collect_machine_info()),
        "cpu_allocation": _stringify_mapping(cpu_allocation),
        "thread_topology": _stringify_mapping(thread_topology),
        "workload": _stringify_mapping(workload),
        "effective_flags": _stringify_mapping(effective_flags),
        "commands": _stringify_mapping(commands),
    }
    if "machine_info_commands" not in context["commands"]:
        context["commands"]["machine_info_commands"] = list(MACHINE_INFO_COMMANDS)
    return context


def attach_benchmark_metadata(
    payload: Mapping[str, Any],
    suite_name: str,
    entry_script: str,
    workload: Optional[Mapping[str, Any]] = None,
    cpu_allocation: Optional[Mapping[str, Any]] = None,
    thread_topology: Optional[Mapping[str, Any]] = None,
    effective_flags: Optional[Mapping[str, Any]] = None,
    commands: Optional[Mapping[str, Any]] = None,
    artifacts: Optional[Mapping[str, Any]] = None,
    machine_info: Optional[Mapping[str, Any]] = None,
    captured_at: Optional[str] = None,
) -> JsonDict:
    annotated = copy.deepcopy(dict(payload))
    # benchmark 的主 JSON 后面会直接被搬去论文资产和结果归档。
    # 所以这里不再额外生成平行 metadata 文件，而是把实验上下文直接嵌回主结果，
    # 这样单独拎走一份 result.json/summary.json 也能自证“这份数据是在什么机器和什么拓扑下跑的”。
    annotated["experiment_context"] = build_experiment_context(
        suite_name=suite_name,
        entry_script=entry_script,
        workload=workload,
        cpu_allocation=cpu_allocation,
        thread_topology=thread_topology,
        effective_flags=effective_flags,
        commands=commands,
        machine_info=machine_info,
        captured_at=captured_at,
    )
    annotated["artifacts"] = _stringify_mapping(artifacts)
    return annotated
