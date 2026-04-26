#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import html
import json
import tarfile
from pathlib import Path
from typing import Any, Dict, List, Sequence, Tuple


JsonDict = Dict[str, Any]
Row = Dict[str, str]

BENCHMARK_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSET_DIR = BENCHMARK_ROOT / "paper_assets"
DEFAULT_ARCHIVE_NAMES = [
    "suite_a_20260418.tar.gz",
    "suite_b_20260418_x10.tar.gz",
    "suite_d_final_curve_rerun_d512_f1024.tar.gz",
    "suite_d_final_peak_20c_d512_f1024.tar.gz",
]


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render LogSentinel benchmark paper assets into CSV, LaTeX tables and SVG figures."
    )
    parser.add_argument(
        "--asset-dir",
        default=str(DEFAULT_ASSET_DIR),
        help="directory containing benchmark paper asset tar.gz files",
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="output directory; default: <asset-dir>/rendered",
    )
    parser.add_argument(
        "--archive",
        action="append",
        default=[],
        help="explicit archive name/path; can be repeated. Defaults to the four formal archives.",
    )
    return parser.parse_args(argv)


def discover_default_archives(asset_dir: Path) -> List[Path]:
    # 论文最终图表只吃这 4 个正式资产包。
    # 旧的 suite_d_20260418.tar.gz 是早期 scaling 口径，保留在目录里用于追溯，但默认不能混进正文图表。
    return [asset_dir / name for name in DEFAULT_ARCHIVE_NAMES if (asset_dir / name).exists()]


def resolve_archives(asset_dir: Path, archive_args: Sequence[str]) -> List[Path]:
    if not archive_args:
        archives = discover_default_archives(asset_dir)
    else:
        archives = []
        for value in archive_args:
            candidate = Path(value)
            archives.append(candidate if candidate.is_absolute() else asset_dir / candidate)

    missing = [str(path) for path in archives if not path.exists()]
    if missing:
        raise FileNotFoundError("missing paper asset archives: " + ", ".join(missing))
    return archives


def load_json_member(archive_path: Path, member_name: str) -> JsonDict:
    # tar.gz 资产包是只读输入；这里直接从压缩包抽 summary JSON，
    # 不把完整 result/log 解压到工作区，避免生成论文图时污染原始资产目录。
    with tarfile.open(archive_path, "r:gz") as archive:
        extracted = archive.extractfile(member_name)
        if extracted is None:
            raise FileNotFoundError(f"{archive_path.name} missing member {member_name}")
        return json.load(extracted)


def fmt_number(value: Any, digits: int = 4) -> str:
    if value is None:
        return "NA"
    if isinstance(value, bool):
        return "true" if value else "false"
    try:
        return f"{float(value):.{digits}f}"
    except (TypeError, ValueError):
        return str(value)


def as_int_text(value: Any) -> str:
    if value is None:
        return "NA"
    try:
        return str(int(value))
    except (TypeError, ValueError):
        return str(value)


def split_case_id(case_id: str) -> Tuple[str, str]:
    parts = case_id.split("__", 1)
    if len(parts) == 2:
        return parts[0], parts[1]
    return "unknown", case_id


def archive_lookup(archives: Sequence[Path]) -> Dict[str, Path]:
    result: Dict[str, Path] = {}
    for path in archives:
        result[path.name] = path
    return result


def build_suite_a_tables(summary: JsonDict, buffer_summary: JsonDict) -> Tuple[List[Row], List[Row]]:
    # Suite A 的论文表只需要 suite 级 summary 中的聚合值。
    # 这里不回读每个 result.json，是为了避免表格数字和正式 summary 口径分叉。
    main_rows: List[Row] = []
    for item in summary.get("by_load", []):
        variants = item.get("variants", {})
        cmp_variant = variants.get("cmp_baseline", item.get("cmp", {}))
        main_variant = variants.get("main_tuned", item.get("main", {}))
        delta = item.get("delta_main_vs_cmp", {})
        main_rows.append(
            {
                "load": str(item.get("label") or item.get("load_name") or item.get("load", {}).get("name") or "unknown"),
                "trace_count": as_int_text(item.get("trace_count")),
                "gap_ms": as_int_text(item.get("gap_ms")),
                "send_workers": as_int_text(item.get("send_workers")),
                "cmp_visible_completion_rate_at_stop": fmt_number(cmp_variant.get("visible_completion_rate_at_stop")),
                "main_visible_completion_rate_at_stop": fmt_number(main_variant.get("visible_completion_rate_at_stop")),
                "delta_visible_completion_rate_at_stop": fmt_number(delta.get("visible_completion_rate_at_stop")),
                "cmp_drain_tail_ms": fmt_number(cmp_variant.get("drain_tail_ms"), 2),
                "main_drain_tail_ms": fmt_number(main_variant.get("drain_tail_ms"), 2),
                "delta_drain_tail_ms": fmt_number(delta.get("drain_tail_ms"), 2),
            }
        )

    buffer_rows: List[Row] = []
    for item in buffer_summary.get("top_candidates", []) or buffer_summary.get("candidates", []):
        buffer_rows.append(
            {
                "candidate": str(item.get("candidate_name") or item.get("name") or "unknown"),
                "visible_completion_rate_at_stop": fmt_number(item.get("visible_completion_rate_at_stop")),
                "drain_tail_ms": fmt_number(item.get("drain_tail_ms"), 2),
            }
        )
    return main_rows, buffer_rows


def build_suite_b_rows(summary: JsonDict) -> List[Row]:
    # Suite B 的 case_id 同时编码生命周期策略和 sender 场景。
    # 拆成两列后，论文里可以直接按 minimal/protected 或 clean/mixed/late 分组画图。
    rows: List[Row] = []
    aggregate = summary.get("aggregate", {})
    correctness = aggregate.get("correctness_by_case", {})
    unique_counts = aggregate.get("sqlite_unique_constraint_fail_count_by_case", {})
    for case_id in sorted(correctness):
        lifecycle_profile, sender_profile = split_case_id(case_id)
        metrics = correctness[case_id]
        unique = unique_counts.get(case_id, {})
        rows.append(
            {
                "case": case_id,
                "lifecycle_profile": lifecycle_profile,
                "sender_profile": sender_profile,
                "trace_completeness_median": fmt_number(metrics.get("trace_completeness_rate", {}).get("median")),
                "trace_completeness_min": fmt_number(metrics.get("trace_completeness_rate", {}).get("min")),
                "trace_pollution_median": fmt_number(metrics.get("trace_pollution_rate", {}).get("median")),
                "duplicate_persistence_median": fmt_number(metrics.get("duplicate_persistence_rate", {}).get("median")),
                "sqlite_unique_constraint_fail_median": fmt_number(unique.get("median"), 2),
            }
        )
    return rows


def build_suite_d_rows(summary: JsonDict) -> List[Row]:
    # Suite D final curve 和 peak 使用同一种字段结构。
    # 统一归一成 backend_cores -> online throughput，避免正文曲线和峰值表使用两套口径。
    rows: List[Row] = []
    for item in summary.get("by_backend_cores", []):
        wrk_metrics = item.get("wrk_metrics", {})
        rows.append(
            {
                "backend_cores": as_int_text(item.get("backend_cores")),
                "requests_per_sec": fmt_number(wrk_metrics.get("requests_per_sec"), 2),
                "online_completed_traces_per_sec": fmt_number(item.get("online_completed_traces_per_sec"), 2),
                "online_completion_ratio": fmt_number(item.get("online_completion_ratio")),
                "drain_tail_ms": as_int_text(item.get("drain_tail_ms")),
            }
        )
    return rows


def write_csv(path: Path, rows: Sequence[Row]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    headers = list(rows[0].keys())
    with path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=headers)
        writer.writeheader()
        writer.writerows(rows)


def latex_escape(value: str) -> str:
    replacements = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
        "~": r"\textasciitilde{}",
        "^": r"\textasciicircum{}",
    }
    return "".join(replacements.get(ch, ch) for ch in value)


def write_latex_table(path: Path, rows: Sequence[Row], caption: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    headers = list(rows[0].keys())
    alignment = "l" * len(headers)
    lines = [
        r"\begin{table}[htbp]",
        r"\centering",
        rf"\caption{{{latex_escape(caption)}}}",
        rf"\begin{{tabular}}{{{alignment}}}",
        r"\hline",
        " & ".join(latex_escape(header) for header in headers) + r" \\",
        r"\hline",
    ]
    for row in rows:
        lines.append(" & ".join(latex_escape(str(row.get(header, ""))) for header in headers) + r" \\")
    lines.extend([r"\hline", r"\end{tabular}", r"\end{table}", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def parse_float(value: str) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def svg_header(width: int, height: int, title: str) -> List[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        "<style>text{font-family:Georgia,serif;font-size:13px;fill:#1f2933}.title{font-size:18px;font-weight:700}.axis{stroke:#334155;stroke-width:1}.grid{stroke:#d9e2ec;stroke-width:1}.note{font-size:12px;fill:#52606d}</style>",
        f'<rect width="{width}" height="{height}" fill="#fbfaf6"/>',
        f'<text x="{width / 2:.0f}" y="28" class="title" text-anchor="middle">{html.escape(title)}</text>',
    ]


def write_grouped_bar_svg(path: Path, rows: Sequence[Row], title: str, x_key: str, series: Sequence[Tuple[str, str, str]]) -> None:
    # 为了让脚本在云机和本机都能直接跑，图形用内置 SVG 生成器；
    # 这里没有引入 matplotlib/seaborn 这类额外依赖，避免论文资产渲染被 Python 环境卡住。
    width, height = 880, 420
    left, right, top, bottom = 80, 40, 60, 70
    plot_w = width - left - right
    plot_h = height - top - bottom
    values = [parse_float(row.get(key, "0")) for row in rows for _, key, _ in series]
    y_max = max(values + [1.0])
    if y_max <= 1.05:
        y_max = 1.0
    else:
        y_max *= 1.08

    lines = svg_header(width, height, title)
    for tick in range(0, 6):
        y_value = y_max * tick / 5
        y = top + plot_h - (y_value / y_max) * plot_h
        lines.append(f'<line x1="{left}" y1="{y:.1f}" x2="{width - right}" y2="{y:.1f}" class="grid"/>')
        lines.append(f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end">{y_value:.2f}</text>')
    lines.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" class="axis"/>')
    lines.append(f'<line x1="{left}" y1="{top + plot_h}" x2="{width - right}" y2="{top + plot_h}" class="axis"/>')

    group_w = plot_w / max(len(rows), 1)
    bar_w = min(32, group_w / (len(series) + 1))
    for row_index, row in enumerate(rows):
        group_x = left + row_index * group_w + group_w / 2
        start_x = group_x - (len(series) * bar_w) / 2
        for series_index, (label, key, color) in enumerate(series):
            value = parse_float(row.get(key, "0"))
            bar_h = 0 if y_max == 0 else (value / y_max) * plot_h
            x = start_x + series_index * bar_w
            y = top + plot_h - bar_h
            lines.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w - 3:.1f}" height="{bar_h:.1f}" fill="{color}"/>')
            lines.append(f'<text x="{x + (bar_w - 3) / 2:.1f}" y="{y - 5:.1f}" text-anchor="middle" class="note">{value:.3f}</text>')
        lines.append(f'<text x="{group_x:.1f}" y="{top + plot_h + 24}" text-anchor="middle">{html.escape(row.get(x_key, ""))}</text>')

    legend_x = left
    for label, _, color in series:
        lines.append(f'<rect x="{legend_x}" y="{height - 28}" width="14" height="14" fill="{color}"/>')
        lines.append(f'<text x="{legend_x + 20}" y="{height - 16}">{html.escape(label)}</text>')
        legend_x += 190
    lines.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def abbreviate_suite_b_case(row: Row) -> str:
    lifecycle_map = {
        "minimal": "M",
        "protected": "P",
    }
    sender_map = {
        "clean_baseline": "CB",
        "late_replay_stress": "LR",
        "mixed_realistic": "MR",
    }
    lifecycle = lifecycle_map.get(row.get("lifecycle_profile", ""), "U")
    sender = sender_map.get(row.get("sender_profile", ""), "UNK")
    return f"{lifecycle}-{sender}"


def write_suite_b_correctness_svg(path: Path, rows: Sequence[Row]) -> None:
    # Suite B 正文主图只讲 completeness。
    # 既然主结论是“生命周期保护能不能保住完整率”，那么 pollution/duplicate 应该留在表里，不要继续往主图里堆字。
    width = 920
    height = 480
    left, right, top, bottom = 90, 40, 70, 130
    plot_w = width - left - right
    plot_h = height - top - bottom
    values = [parse_float(row.get("trace_completeness_median", "0")) for row in rows]
    y_max = max(values + [1.0])
    if y_max <= 1.05:
        y_max = 1.0
    else:
        y_max *= 1.05

    lines = svg_header(width, height, "Suite B Completeness Under Dirty Timing")
    for tick in range(0, 6):
        y_value = y_max * tick / 5
        y = top + plot_h - (y_value / y_max) * plot_h
        lines.append(f'<line x1="{left}" y1="{y:.1f}" x2="{width - right}" y2="{y:.1f}" class="grid"/>')
        lines.append(f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end">{y_value:.2f}</text>')
    lines.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" class="axis"/>')
    lines.append(f'<line x1="{left}" y1="{top + plot_h}" x2="{width - right}" y2="{top + plot_h}" class="axis"/>')

    group_w = plot_w / max(len(rows), 1)
    bar_w = min(54, group_w * 0.58)
    minimal_color = "#b45309"
    protected_color = "#0f766e"

    for row_index, row in enumerate(rows):
        value = parse_float(row.get("trace_completeness_median", "0"))
        group_x = left + row_index * group_w + group_w / 2
        x = group_x - bar_w / 2
        bar_h = 0 if y_max == 0 else (value / y_max) * plot_h
        y = top + plot_h - bar_h
        color = minimal_color if row.get("lifecycle_profile") == "minimal" else protected_color
        if bar_h > 0:
            lines.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" height="{bar_h:.1f}" fill="{color}" rx="3"/>')
        else:
            # 0 值虽然没有柱高，但仍然是需要强调的结果，所以单独补一个短标记。
            lines.append(
                f'<line x1="{x:.1f}" y1="{top + plot_h - 1:.1f}" x2="{x + bar_w:.1f}" y2="{top + plot_h - 1:.1f}" stroke="{color}" stroke-width="4"/>'
            )

        # 既然正文主图现在只剩 completeness，一共就 6 根柱子，
        # 那每根柱都应该把数值贴出来，避免读者还得来回对 y 轴估读。
        label_y = y - 6 if bar_h > 18 else top + plot_h - 8
        lines.append(f'<text x="{group_x:.1f}" y="{label_y:.1f}" text-anchor="middle" class="note">{value:.2f}</text>')

        lines.append(f'<text x="{group_x:.1f}" y="{top + plot_h + 24}" text-anchor="middle">{html.escape(abbreviate_suite_b_case(row))}</text>')

    separator_x = left + group_w * 3
    lines.append(f'<line x1="{separator_x:.1f}" y1="{top}" x2="{separator_x:.1f}" y2="{top + plot_h}" stroke="#94a3b8" stroke-dasharray="6 4" stroke-width="1.5"/>')
    lines.append(f'<text x="{left + group_w * 1.5:.1f}" y="{top - 14}" text-anchor="middle" class="note">minimal</text>')
    lines.append(f'<text x="{left + group_w * 4.5:.1f}" y="{top - 14}" text-anchor="middle" class="note">protected</text>')
    lines.append(f'<text x="{width / 2:.1f}" y="{height - 44}" text-anchor="middle" class="note">M=minimal, P=protected</text>')
    lines.append(f'<text x="{width / 2:.1f}" y="{height - 24}" text-anchor="middle" class="note">CB=clean baseline, LR=late replay stress, MR=mixed realistic</text>')
    lines.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_line_svg(path: Path, rows: Sequence[Row], title: str, x_key: str, y_key: str, y_label: str) -> None:
    width, height = 880, 420
    left, right, top, bottom = 90, 40, 60, 70
    plot_w = width - left - right
    plot_h = height - top - bottom
    points = [(parse_float(row.get(x_key, "0")), parse_float(row.get(y_key, "0"))) for row in rows]
    x_values = [point[0] for point in points] or [0.0]
    y_values = [point[1] for point in points] or [0.0]
    x_min, x_max = min(x_values), max(x_values)
    if x_min == x_max:
        x_min -= 1
        x_max += 1
    y_max = max(y_values + [1.0]) * 1.1

    def sx(value: float) -> float:
        return left + ((value - x_min) / (x_max - x_min)) * plot_w

    def sy(value: float) -> float:
        return top + plot_h - (value / y_max) * plot_h

    lines = svg_header(width, height, title)
    for tick in range(0, 6):
        y_value = y_max * tick / 5
        y = sy(y_value)
        lines.append(f'<line x1="{left}" y1="{y:.1f}" x2="{width - right}" y2="{y:.1f}" class="grid"/>')
        lines.append(f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end">{y_value:.0f}</text>')
    lines.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" class="axis"/>')
    lines.append(f'<line x1="{left}" y1="{top + plot_h}" x2="{width - right}" y2="{top + plot_h}" class="axis"/>')
    if points:
        polyline = " ".join(f"{sx(x):.1f},{sy(y):.1f}" for x, y in points)
        lines.append(f'<polyline points="{polyline}" fill="none" stroke="#0f766e" stroke-width="3"/>')
        for x, y in points:
            px, py = sx(x), sy(y)
            lines.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="5" fill="#0f766e"/>')
            lines.append(f'<text x="{px:.1f}" y="{py - 10:.1f}" text-anchor="middle" class="note">{y:.0f}</text>')
            lines.append(f'<text x="{px:.1f}" y="{top + plot_h + 24}" text-anchor="middle">{x:.0f}</text>')
    lines.append(f'<text x="{width / 2:.0f}" y="{height - 18}" text-anchor="middle">backend cores</text>')
    lines.append(f'<text x="18" y="{height / 2:.0f}" transform="rotate(-90 18 {height / 2:.0f})" text-anchor="middle">{html.escape(y_label)}</text>')
    lines.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_peak_svg(path: Path, rows: Sequence[Row]) -> None:
    width, height = 720, 260
    row = rows[0] if rows else {}
    backend = row.get("backend_cores", "NA")
    online = row.get("online_completed_traces_per_sec", "NA")
    qps = row.get("requests_per_sec", "NA")
    ratio = row.get("online_completion_ratio", "NA")
    lines = svg_header(width, height, "Suite D Peak Point")
    lines.extend(
        [
            '<rect x="70" y="70" width="580" height="130" rx="18" fill="#e0f2fe" stroke="#0369a1" stroke-width="2"/>',
            f'<text x="110" y="118" class="title">backend cores: {html.escape(backend)}</text>',
            f'<text x="110" y="154">online completed traces/sec: {html.escape(online)}</text>',
            f'<text x="110" y="180">wrk requests/sec: {html.escape(qps)} · online ratio: {html.escape(ratio)}</text>',
            "</svg>",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_table_bundle(tables_dir: Path, name: str, rows: Sequence[Row], caption: str) -> List[str]:
    csv_path = tables_dir / f"{name}.csv"
    tex_path = tables_dir / f"{name}.tex"
    write_csv(csv_path, rows)
    write_latex_table(tex_path, rows, caption)
    return [str(csv_path), str(tex_path)]


def relative_output_path(output_dir: Path, path_text: str) -> str:
    # manifest 进入仓库后不能写死本机绝对路径；
    # 这里把派生文件路径压成相对 rendered/ 的形式，方便别人换目录后仍能复核。
    path = Path(path_text)
    try:
        return str(path.relative_to(output_dir))
    except ValueError:
        return str(path)


def render_assets(args: argparse.Namespace) -> JsonDict:
    # 主编排函数只做“读正式 tar 包 -> 写派生图表”。
    # 生成物统一放在 rendered/ 下，原始 tar.gz 不会被修改，方便后续反复重跑。
    asset_dir = Path(args.asset_dir)
    output_dir = Path(args.output_dir) if args.output_dir else asset_dir / "rendered"
    tables_dir = output_dir / "paper_tables"
    figures_dir = output_dir / "paper_figures"
    archives = resolve_archives(asset_dir, args.archive)
    by_name = archive_lookup(archives)

    suite_a = by_name["suite_a_20260418.tar.gz"]
    suite_b = by_name["suite_b_20260418_x10.tar.gz"]
    suite_d_curve = by_name["suite_d_final_curve_rerun_d512_f1024.tar.gz"]
    suite_d_peak = by_name["suite_d_final_peak_20c_d512_f1024.tar.gz"]

    suite_a_main, suite_a_buffer = build_suite_a_tables(
        load_json_member(suite_a, "suite_a/summary/main_vs_cmp_summary.json"),
        load_json_member(suite_a, "suite_a/summary/buffer_compare_summary.json"),
    )
    suite_b_rows = build_suite_b_rows(load_json_member(suite_b, "suite_b/summary/campaign_summary_x10.json"))
    suite_d_curve_rows = build_suite_d_rows(
        load_json_member(suite_d_curve, "final_curve_rerun_d512_f1024/summary/scaling_summary.json")
    )
    suite_d_peak_rows = build_suite_d_rows(
        load_json_member(suite_d_peak, "final_peak_20c_d512_f1024/summary/scaling_summary.json")
    )

    tables: List[str] = []
    tables += write_table_bundle(tables_dir, "suite_a_main_vs_cmp", suite_a_main, "Suite A main versus compare target")
    tables += write_table_bundle(tables_dir, "suite_a_buffer_compare", suite_a_buffer, "Suite A buffered repository attribution")
    tables += write_table_bundle(tables_dir, "suite_b_correctness", suite_b_rows, "Suite B lifecycle correctness")
    tables += write_table_bundle(tables_dir, "suite_d_fixed_load_curve", suite_d_curve_rows, "Suite D fixed-load scaling curve")
    tables += write_table_bundle(tables_dir, "suite_d_peak", suite_d_peak_rows, "Suite D peak point")

    write_grouped_bar_svg(
        figures_dir / "suite_a_main_vs_cmp.svg",
        suite_a_main,
        "Suite A Visible Completion Rate",
        "load",
        [
            ("compare target", "cmp_visible_completion_rate_at_stop", "#b45309"),
            ("main tuned", "main_visible_completion_rate_at_stop", "#0f766e"),
        ],
    )
    write_grouped_bar_svg(
        figures_dir / "suite_a_buffer_compare.svg",
        suite_a_buffer,
        "Suite A Buffer Attribution",
        "candidate",
        [("visible completion", "visible_completion_rate_at_stop", "#0f766e")],
    )
    write_suite_b_correctness_svg(figures_dir / "suite_b_correctness.svg", suite_b_rows)
    write_line_svg(
        figures_dir / "suite_d_fixed_load_curve.svg",
        suite_d_curve_rows,
        "Suite D Fixed-Load Scaling",
        "backend_cores",
        "online_completed_traces_per_sec",
        "online completed traces/sec",
    )
    write_peak_svg(figures_dir / "suite_d_peak.svg", suite_d_peak_rows)

    figures = [str(path) for path in sorted(figures_dir.glob("*.svg"))]
    # manifest 是归档索引，不是运行日志；路径统一写成相对 rendered/，
    # 这样换机器或换仓库目录后，索引仍然能直接对应同级文件。
    manifest = {
        "archives": [path.name for path in archives],
        "output_dir": ".",
        "tables": [relative_output_path(output_dir, table) for table in tables],
        "figures": [relative_output_path(output_dir, figure) for figure in figures],
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return manifest


def main() -> None:
    args = parse_args()
    asset_dir = Path(args.asset_dir)
    output_dir = Path(args.output_dir) if args.output_dir else asset_dir / "rendered"
    manifest = render_assets(args)
    print(f"[paper_assets_rendered] output_dir={output_dir}")
    print("[archives]")
    for archive in manifest["archives"]:
        print(f"- {archive}")
    print("[tables]")
    for table in manifest["tables"]:
        print(f"- {table}")
    print("[figures]")
    for figure in manifest["figures"]:
        print(f"- {figure}")


if __name__ == "__main__":
    main()
