#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict


@dataclass(frozen=True)
class SenderProfile:
    name: str
    head_weights: Dict[str, int]
    body_weights: Dict[str, int]
    tail_weights: Dict[str, int]


# 这里先把三档 profile 写成普通字典，避免引入 yaml/toml 之类额外依赖。
# 后面如果要把概率暴露成外部配置，再把这个表迁出去即可。
_PROFILES: Dict[str, SenderProfile] = {
    "clean_baseline": SenderProfile(
        name="clean_baseline",
        head_weights={"clean_jitter": 100},
        body_weights={"clean_jitter": 100},
        tail_weights={"clean_jitter": 100},
    ),
    "mixed_realistic": SenderProfile(
        name="mixed_realistic",
        head_weights={
            "clean_jitter": 95,
            "reorder_in_grace": 4,
            "late_after_dispatch": 1,
        },
        body_weights={
            "clean_jitter": 84,
            "reorder_in_grace": 10,
            "late_after_dispatch": 5,
            "replay_clone": 1,
        },
        tail_weights={
            "clean_jitter": 98,
            "reorder_in_grace": 2,
        },
    ),
    "late_replay_stress": SenderProfile(
        name="late_replay_stress",
        head_weights={
            "clean_jitter": 88,
            "reorder_in_grace": 8,
            "late_after_dispatch": 4,
        },
        body_weights={
            "clean_jitter": 65,
            "reorder_in_grace": 15,
            "late_after_dispatch": 15,
            "replay_clone": 5,
        },
        tail_weights={
            "clean_jitter": 92,
            "reorder_in_grace": 5,
            "late_after_dispatch": 3,
        },
    ),
}


def get_sender_profile(name: str) -> SenderProfile:
    try:
        return _PROFILES[name]
    except KeyError as exc:
        # 这里主动列出可选项，避免 benchmark 跑到一半才发现 profile 名字写错。
        available = ", ".join(sorted(_PROFILES))
        raise ValueError(f"unsupported suite_b sender profile: {name}, available: {available}") from exc
