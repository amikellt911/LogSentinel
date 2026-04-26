#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
import struct
from pathlib import Path
from typing import Sequence
from xml.etree import ElementTree as ET
from xml.sax.saxutils import escape
from zipfile import ZipFile, ZipInfo


WORD_NS = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
NS = {"w": WORD_NS}
TABLES_DIR = Path("server/tests/benchmark/paper_assets/rendered/paper_tables")
FIGURES_PNG_DIR = Path("server/tests/benchmark/paper_assets/rendered/paper_figures_png")
FRAGMENT_WRAPPER = (
    '<fragment '
    'xmlns:wpc="http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas" '
    'xmlns:cx="http://schemas.microsoft.com/office/drawing/2014/chartex" '
    'xmlns:cx1="http://schemas.microsoft.com/office/drawing/2015/9/8/chartex" '
    'xmlns:cx2="http://schemas.microsoft.com/office/drawing/2015/10/21/chartex" '
    'xmlns:cx3="http://schemas.microsoft.com/office/drawing/2016/5/9/chartex" '
    'xmlns:cx4="http://schemas.microsoft.com/office/drawing/2016/5/10/chartex" '
    'xmlns:cx5="http://schemas.microsoft.com/office/drawing/2016/5/11/chartex" '
    'xmlns:cx6="http://schemas.microsoft.com/office/drawing/2016/5/12/chartex" '
    'xmlns:cx7="http://schemas.microsoft.com/office/drawing/2016/5/13/chartex" '
    'xmlns:cx8="http://schemas.microsoft.com/office/drawing/2016/5/14/chartex" '
    'xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" '
    'xmlns:aink="http://schemas.microsoft.com/office/drawing/2016/ink" '
    'xmlns:am3d="http://schemas.microsoft.com/office/drawing/2017/model3d" '
    'xmlns:o="urn:schemas-microsoft-com:office:office" '
    'xmlns:oel="http://schemas.microsoft.com/office/2019/extlst" '
    'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
    'xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math" '
    'xmlns:v="urn:schemas-microsoft-com:vml" '
    'xmlns:wp14="http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing" '
    'xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" '
    'xmlns:w10="urn:schemas-microsoft-com:office:word" '
    'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
    'xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml" '
    'xmlns:w15="http://schemas.microsoft.com/office/word/2012/wordml" '
    'xmlns:w16cex="http://schemas.microsoft.com/office/word/2018/wordml/cex" '
    'xmlns:w16cid="http://schemas.microsoft.com/office/word/2016/wordml/cid" '
    'xmlns:w16="http://schemas.microsoft.com/office/word/2018/wordml" '
    'xmlns:w16du="http://schemas.microsoft.com/office/word/2023/wordml/word16du" '
    'xmlns:w16sdtdh="http://schemas.microsoft.com/office/word/2020/wordml/sdtdatahash" '
    'xmlns:w16sdtfl="http://schemas.microsoft.com/office/word/2024/wordml/sdtformatlock" '
    'xmlns:w16se="http://schemas.microsoft.com/office/word/2015/wordml/symex" '
    'xmlns:wpg="http://schemas.microsoft.com/office/word/2010/wordprocessingGroup" '
    'xmlns:wpi="http://schemas.microsoft.com/office/word/2010/wordprocessingInk" '
    'xmlns:wne="http://schemas.microsoft.com/office/word/2006/wordml" '
    'xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape">'
    "{fragment}"
    "</fragment>"
)
FIGURE_ALIAS_TO_FILE = {
    "fig_5_1": "suite_b_correctness.png",
    "fig_5_2": "suite_a_main_vs_cmp.png",
    "fig_5_3": "suite_a_buffer_compare.png",
    "fig_5_4": "suite_d_fixed_load_curve.png",
    "fig_5_5": "suite_d_peak.png",
}


class XmlBuildState:
    def __init__(self, *, bookmark_id_start: int = 153, bookmark_name_start: int = 500001, docpr_id_start: int = 200):
        self.bookmark_id = bookmark_id_start
        self.bookmark_name = bookmark_name_start
        self.docpr_id = docpr_id_start

    def next_bookmark(self) -> tuple[int, str]:
        current_id = self.bookmark_id
        current_name = f"_Toc{self.bookmark_name}"
        self.bookmark_id += 1
        self.bookmark_name += 1
        return current_id, current_name

    def next_docpr_id(self) -> int:
        current_id = self.docpr_id
        self.docpr_id += 1
        return current_id


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="从安全底稿出发，把正文图表、附录图表题和目录更新能力安全并入论文。"
    )
    parser.add_argument("--input", default="毕设论文初稿草稿.docx", help="安全底稿 docx 路径")
    parser.add_argument(
        "--output",
        default="毕设论文初稿草稿_正文附录图表目录版.docx",
        help="输出 docx 路径",
    )
    parser.add_argument(
        "--figure-png-dir",
        default=str(FIGURES_PNG_DIR),
        help="SVG 转好的 PNG 目录",
    )
    return parser.parse_args()


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def split_body_blocks(document_xml: str) -> tuple[str, list[str], str]:
    """只切分 body 顶层块，保留原始 document 根标签和命名空间声明。

    这份论文模板对 document.xml 根部的兼容命名空间非常敏感。
    一旦整树重写，Word 很容易弹“发现无法读取的内容”。
    所以这里始终坚持：原始头尾不动，只在 body 的顶层块之间插入新片段。
    """

    body_start = document_xml.index("<w:body")
    body_open_end = document_xml.index(">", body_start) + 1
    body_close_start = document_xml.rindex("</w:body>")
    body_inner = document_xml[body_open_end:body_close_start]

    blocks: list[str] = []
    depth = 0
    block_start: int | None = None
    for match in re.finditer(r"<[^>]+>", body_inner):
        tag = match.group(0)
        if tag.startswith("<?") or tag.startswith("<!"):
            continue

        is_closing = tag.startswith("</")
        is_self_closing = tag.endswith("/>")
        if not is_closing and depth == 0:
            block_start = match.start()

        if is_closing:
            depth -= 1
        elif not is_self_closing:
            depth += 1

        if depth == 0 and block_start is not None:
            blocks.append(body_inner[block_start:match.end()])
            block_start = None

    return document_xml[:body_open_end], blocks, document_xml[body_close_start:]


def block_text(block_xml: str) -> str:
    element = ET.fromstring(FRAGMENT_WRAPPER.format(fragment=block_xml))[0]
    return "".join(node.text or "" for node in element.findall(".//w:t", NS)).strip()


def make_paragraph(
    text: str,
    *,
    style_id: str,
    center: bool = False,
    first_line_indent: bool = True,
    bold: bool = False,
    bookmark_name: str | None = None,
    bookmark_id: int | None = None,
) -> str:
    ppr_parts = [f'<w:pStyle w:val="{style_id}"/>']
    if not first_line_indent:
        ppr_parts.append('<w:ind w:firstLineChars="0" w:firstLine="0"/>')
    if center:
        ppr_parts.append('<w:jc w:val="center"/>')

    parts = ["<w:p>", f"<w:pPr>{''.join(ppr_parts)}</w:pPr>"]
    if bookmark_name is not None and bookmark_id is not None:
        parts.append(f'<w:bookmarkStart w:id="{bookmark_id}" w:name="{bookmark_name}"/>')

    run_parts = ["<w:r>"]
    if bold:
        run_parts.append("<w:rPr><w:b/></w:rPr>")
    run_parts.append(f"<w:t>{escape(text)}</w:t></w:r>")
    parts.append("".join(run_parts))

    if bookmark_name is not None and bookmark_id is not None:
        parts.append(f'<w:bookmarkEnd w:id="{bookmark_id}"/>')
    parts.append("</w:p>")
    return "".join(parts)


def make_blank_paragraph() -> str:
    return '<w:p><w:pPr><w:pStyle w:val="affa"/></w:pPr></w:p>'


def make_cell_paragraph(text: str, *, bold: bool = False) -> str:
    rpr = "<w:rPr><w:b/></w:rPr>" if bold else ""
    return (
        "<w:p>"
        "<w:pPr>"
        '<w:pStyle w:val="affa"/>'
        '<w:ind w:firstLineChars="0" w:firstLine="0"/>'
        '<w:jc w:val="center"/>'
        "</w:pPr>"
        f"<w:r>{rpr}<w:t>{escape(text)}</w:t></w:r>"
        "</w:p>"
    )


def normalize_widths(column_weights: Sequence[int], total_width: int = 9061) -> list[int]:
    weight_sum = sum(column_weights)
    widths: list[int] = []
    consumed = 0
    for index, weight in enumerate(column_weights):
        if index == len(column_weights) - 1:
            widths.append(total_width - consumed)
        else:
            width = total_width * weight // weight_sum
            widths.append(width)
            consumed += width
    return widths


def make_table(headers: Sequence[str], rows: Sequence[Sequence[str]], column_weights: Sequence[int]) -> str:
    widths = normalize_widths(column_weights)
    grid = "".join(f'<w:gridCol w:w="{width}"/>' for width in widths)
    table_rows: list[str] = []
    for row_index, row in enumerate([headers, *rows]):
        cells = []
        for width, value in zip(widths, row):
            cells.append(
                "<w:tc>"
                f'<w:tcPr><w:tcW w:w="{width}" w:type="dxa"/></w:tcPr>'
                f"{make_cell_paragraph(value, bold=row_index == 0)}"
                "</w:tc>"
            )
        table_rows.append(f"<w:tr>{''.join(cells)}</w:tr>")

    return (
        "<w:tbl>"
        "<w:tblPr>"
        '<w:tblStyle w:val="af4"/>'
        '<w:tblW w:w="0" w:type="auto"/>'
        '<w:tblLook w:val="04A0" w:firstRow="1" w:lastRow="0" '
        'w:firstColumn="1" w:lastColumn="0" w:noHBand="0" w:noVBand="1"/>'
        "</w:tblPr>"
        f"<w:tblGrid>{grid}</w:tblGrid>"
        f"{''.join(table_rows)}"
        "</w:tbl>"
    )


def read_png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} 不是合法 PNG")
    width, height = struct.unpack(">II", data[16:24])
    return width, height


def scale_emu(width_px: int, height_px: int, *, max_width_emu: int = 5_200_000) -> tuple[int, int]:
    scaled_height = max(1, round(max_width_emu * height_px / width_px))
    return max_width_emu, scaled_height


def make_image_paragraph(rid: str, *, cx: int, cy: int, docpr_id: int, name: str) -> str:
    """图片段只走 Word 最常见的 inline 图片骨架，避免引入额外兼容分支。

    这次图片不是直接塞 SVG，而是先转成 PNG，再挂现成的 image relationship。
    原因很简单：PNG 的 Word 兼容性最稳，不需要额外 fallback 图或 asvg 扩展。
    """

    return (
        "<w:p>"
        "<w:pPr>"
        '<w:pStyle w:val="aff7"/>'
        '<w:ind w:firstLineChars="0" w:firstLine="0"/>'
        '<w:jc w:val="center"/>'
        "</w:pPr>"
        "<w:r>"
        "<w:rPr><w:noProof/></w:rPr>"
        "<w:drawing>"
        '<wp:inline xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" '
        'distT="0" distB="0" distL="114300" distR="114300">'
        f'<wp:extent cx="{cx}" cy="{cy}"/>'
        '<wp:effectExtent l="0" t="0" r="12700" b="12700"/>'
        f'<wp:docPr id="{docpr_id}" name="{escape(name)}"/>'
        '<wp:cNvGraphicFramePr>'
        '<a:graphicFrameLocks xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" noChangeAspect="1"/>'
        "</wp:cNvGraphicFramePr>"
        '<a:graphic xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">'
        '<a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture">'
        '<pic:pic xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture">'
        "<pic:nvPicPr>"
        f'<pic:cNvPr id="{docpr_id}" name="{escape(name)}"/>'
        "<pic:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></pic:cNvPicPr>"
        "</pic:nvPicPr>"
        f'<pic:blipFill><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>'
        "<pic:spPr>"
        "<a:xfrm><a:off x=\"0\" y=\"0\"/>"
        f'<a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom>'
        "<a:noFill/><a:ln><a:noFill/></a:ln>"
        "</pic:spPr>"
        "</pic:pic>"
        "</a:graphicData>"
        "</a:graphic>"
        "</wp:inline>"
        "</w:drawing>"
        "</w:r>"
        "</w:p>"
    )


def map_sender_profile(profile: str) -> str:
    return {
        "clean_baseline": "干净基线",
        "mixed_realistic": "混合真实脏时序",
        "late_replay_stress": "晚到重放压力",
    }[profile]


def map_load(load: str) -> str:
    return {"light": "轻负载", "mid": "中负载", "heavy": "重负载"}[load]


def build_experiment_one_rows() -> list[list[str]]:
    rows = read_csv_rows(TABLES_DIR / "suite_b_correctness.csv")
    sender_order = {"clean_baseline": 0, "mixed_realistic": 1, "late_replay_stress": 2}
    lifecycle_order = {"protected": 0, "minimal": 1}
    rows.sort(key=lambda row: (sender_order[row["sender_profile"]], lifecycle_order[row["lifecycle_profile"]]))
    return [
        [
            row["lifecycle_profile"],
            map_sender_profile(row["sender_profile"]),
            row["trace_completeness_median"],
            row["trace_completeness_min"],
            row["trace_pollution_median"],
            row["duplicate_persistence_median"],
            row["sqlite_unique_constraint_fail_median"],
        ]
        for row in rows
    ]


def build_experiment_two_main_rows() -> list[list[str]]:
    rows = read_csv_rows(TABLES_DIR / "suite_a_main_vs_cmp.csv")
    return [
        [
            map_load(row["load"]),
            row["trace_count"],
            row["gap_ms"],
            row["cmp_visible_completion_rate_at_stop"],
            row["main_visible_completion_rate_at_stop"],
            row["delta_visible_completion_rate_at_stop"],
            row["cmp_drain_tail_ms"],
            row["main_drain_tail_ms"],
            row["delta_drain_tail_ms"],
        ]
        for row in rows
    ]


def build_experiment_two_buffer_rows() -> list[list[str]]:
    rows = read_csv_rows(TABLES_DIR / "suite_a_buffer_compare.csv")
    candidate_map = {"buffered_512_5": "启用缓冲写入", "disable_buffered": "关闭缓冲写入"}
    order = {"启用缓冲写入": 0, "关闭缓冲写入": 1}
    normalized_rows = [
        [
            candidate_map[row["candidate"]],
            row["visible_completion_rate_at_stop"],
            row["drain_tail_ms"],
        ]
        for row in rows
    ]
    normalized_rows.sort(key=lambda row: order[row[0]])
    return normalized_rows


def build_experiment_three_curve_rows() -> list[list[str]]:
    rows = read_csv_rows(TABLES_DIR / "suite_d_fixed_load_curve.csv")
    return [
        [
            row["backend_cores"],
            row["requests_per_sec"],
            row["online_completed_traces_per_sec"],
            row["online_completion_ratio"],
            row["drain_tail_ms"],
        ]
        for row in rows
    ]


def build_experiment_three_peak_rows() -> list[list[str]]:
    rows = read_csv_rows(TABLES_DIR / "suite_d_peak.csv")
    return [
        [
            row["backend_cores"],
            row["requests_per_sec"],
            row["online_completed_traces_per_sec"],
            row["online_completion_ratio"],
            row["drain_tail_ms"],
        ]
        for row in rows
    ]


def make_caption(state: XmlBuildState, text: str, *, style_id: str) -> str:
    bookmark_id, bookmark_name = state.next_bookmark()
    return make_paragraph(
        text,
        style_id=style_id,
        center=True,
        first_line_indent=False,
        bookmark_name=bookmark_name,
        bookmark_id=bookmark_id,
    )


def make_figure_group(state: XmlBuildState, figure_meta: dict[str, str | int], caption_text: str) -> list[str]:
    return [
        make_image_paragraph(
            str(figure_meta["rid"]),
            cx=int(figure_meta["cx"]),
            cy=int(figure_meta["cy"]),
            docpr_id=state.next_docpr_id(),
            name=str(figure_meta["name"]),
        ),
        make_caption(state, caption_text, style_id="afe"),
    ]


def build_main_section_fragments(state: XmlBuildState, figure_map: dict[str, dict[str, str | int]]) -> dict[str, list[str]]:
    experiment_one_table = make_table(
        ["生命周期配置", "输入画像", "完整率中位数", "完整率最小值", "污染率中位数", "重复持久化率中位数", "唯一键冲突中位数"],
        build_experiment_one_rows(),
        [12, 20, 12, 12, 12, 16, 16],
    )
    experiment_two_main_table = make_table(
        ["负载档位", "Trace数", "间隔/ms", "历史完成率", "当前完成率", "完成率差值", "历史排空/ms", "当前排空/ms", "排空差值/ms"],
        build_experiment_two_main_rows(),
        [9, 8, 8, 12, 12, 12, 13, 13, 13],
    )
    experiment_two_buffer_table = make_table(
        ["配置", "停止发流时可见完成率", "尾部排空时间/ms"],
        build_experiment_two_buffer_rows(),
        [24, 20, 16],
    )
    experiment_three_curve_table = make_table(
        ["后端可用核数", "wrk请求速率", "窗口内完成吞吐", "窗口内完成率", "尾部排空时间/ms"],
        build_experiment_three_curve_rows(),
        [10, 18, 18, 14, 14],
    )
    experiment_three_peak_table = make_table(
        ["后端可用核数", "wrk请求速率", "窗口内完成吞吐", "窗口内完成率", "尾部排空时间/ms"],
        build_experiment_three_peak_rows(),
        [10, 18, 18, 14, 14],
    )

    # 正文这里同时补主图和主表。
    # 图负责让读者先看到趋势和对比强弱，表负责给出可引用的精确数值；
    # 两者都放在分析小节前，分析段落就能直接“见图/见表”引用。
    return {
        "5.2.3 机制验证结果分析": [
            make_paragraph(
                "实验一生命周期保护正确性主图如图5.1所示，汇总结果如表5.1所示，详细实验参数与指标口径见附录B.2。",
                style_id="affa",
            ),
            *make_figure_group(state, figure_map["fig_5_1"], "图5.1 实验一生命周期保护正确性结果图"),
            make_caption(state, "表5.1 实验一生命周期保护正确性结果汇总", style_id="afd"),
            experiment_one_table,
            make_blank_paragraph(),
        ],
        "5.3.3 开销结果分析": [
            make_paragraph(
                "实验二主体对照线结果如图5.2和表5.2所示，缓冲写入归因结果如图5.3和表5.3所示，详细测试环境与参数口径见附录B.1和附录B.3。",
                style_id="affa",
            ),
            *make_figure_group(state, figure_map["fig_5_2"], "图5.2 实验二主体对照线可见完成率结果图"),
            *make_figure_group(state, figure_map["fig_5_3"], "图5.3 实验二缓冲写入归因结果图"),
            make_caption(state, "表5.2 实验二主体对照线结果汇总", style_id="afd"),
            experiment_two_main_table,
            make_caption(state, "表5.3 实验二缓冲写入归因结果汇总", style_id="afd"),
            experiment_two_buffer_table,
            make_blank_paragraph(),
        ],
        "5.4.3 能力边界与瓶颈分析": [
            make_paragraph(
                "实验三固定负载主曲线如图5.4和表5.4所示，峰值点结果如图5.5和表5.5所示，详细诊断量与环境口径见附录B.1和附录B.4。",
                style_id="affa",
            ),
            *make_figure_group(state, figure_map["fig_5_4"], "图5.4 实验三固定负载主曲线结果图"),
            *make_figure_group(state, figure_map["fig_5_5"], "图5.5 实验三峰值点结果图"),
            make_caption(state, "表5.4 实验三固定负载主曲线结果汇总", style_id="afd"),
            experiment_three_curve_table,
            make_caption(state, "表5.5 实验三峰值点结果汇总", style_id="afd"),
            experiment_three_peak_table,
            make_blank_paragraph(),
        ],
    }


def replace_exact_block_text(blocks: list[str], target_text: str, replacement_block: str) -> None:
    for index, block in enumerate(blocks):
        if block_text(block) == target_text:
            blocks[index] = replacement_block
            return
    raise ValueError(f"未找到待替换块：{target_text}")


def insert_before_exact_block_text(blocks: list[str], target_text: str, fragments: Sequence[str]) -> None:
    for index, block in enumerate(blocks):
        if block_text(block) == target_text:
            blocks[index:index] = list(fragments)
            return
    raise ValueError(f"未找到插入锚点：{target_text}")


def insert_after_exact_block_text(blocks: list[str], target_text: str, fragments: Sequence[str]) -> None:
    for index, block in enumerate(blocks):
        if block_text(block) == target_text:
            blocks[index + 1:index + 1] = list(fragments)
            return
    raise ValueError(f"未找到插入锚点：{target_text}")


def insert_after_block_prefix(blocks: list[str], target_prefix: str, fragments: Sequence[str]) -> None:
    for index, block in enumerate(blocks):
        if block_text(block).startswith(target_prefix):
            blocks[index + 1:index + 1] = list(fragments)
            return
    raise ValueError(f"未找到前缀插入锚点：{target_prefix}")


def insert_before_nth_block_text(blocks: list[str], target_text: str, occurrence: int, fragments: Sequence[str]) -> None:
    hit_count = 0
    for index, block in enumerate(blocks):
        if block_text(block) == target_text:
            hit_count += 1
            if hit_count == occurrence:
                blocks[index:index] = list(fragments)
                return
    raise ValueError(f"未找到第 {occurrence} 个插入锚点：{target_text}")


def insert_before_nth_block_prefix(blocks: list[str], target_prefix: str, occurrence: int, fragments: Sequence[str]) -> None:
    hit_count = 0
    for index, block in enumerate(blocks):
        if block_text(block).startswith(target_prefix):
            hit_count += 1
            if hit_count == occurrence:
                blocks[index:index] = list(fragments)
                return
    raise ValueError(f"未找到第 {occurrence} 个前缀锚点：{target_prefix}")


def apply_main_section_insertions(blocks: list[str], fragments_map: dict[str, list[str]]) -> None:
    for anchor_text, fragments in fragments_map.items():
        insert_before_exact_block_text(blocks, anchor_text, fragments)


def apply_appendix_insertions(blocks: list[str], state: XmlBuildState, figure_map: dict[str, dict[str, str | int]]) -> None:
    # 附录里的现有表原本只是“说明文字 + 裸表格”。
    # 这里把它们补成规范表题，同时把 rendered 主图按 B.2/B.3/B.4 分组放进去，
    # 这样附录既能承接补充证据，也不会抢正文主叙事的位置。
    insert_before_nth_block_prefix(
        blocks,
        "参数项正式口径",
        1,
        [make_caption(state, "表B.1 正式测试环境补充说明表", style_id="afd")],
    )
    insert_after_block_prefix(
        blocks,
        "实验一用于验证生命周期保护机制在构造化脏时序输入下对 Trace 聚合正确性的影响。",
        make_figure_group(state, figure_map["fig_5_1"], "图B.1 实验一正确性补充结果图"),
    )
    insert_before_nth_block_prefix(
        blocks,
        "场景随机种子对照配置Trace 数每条 Trace 的 Span 数发送端 workerAIWebhook观察目标",
        1,
        [make_caption(state, "表B.2 实验一参数与指标口径表", style_id="afd")],
    )

    insert_after_block_prefix(
        blocks,
        "实验二用于回答“为了做对，当前主线付出了多大代价”。",
        [
            *make_figure_group(state, figure_map["fig_5_2"], "图B.2 实验二主体对照线补充结果图"),
            *make_figure_group(state, figure_map["fig_5_3"], "图B.3 实验二缓冲写入归因补充结果图"),
        ],
    )
    replace_exact_block_text(blocks, "主体对照线正式口径", make_caption(state, "表B.3 实验二主体对照线正式口径表", style_id="afd"))
    replace_exact_block_text(blocks, "补充对照线正式口径", make_caption(state, "表B.4 实验二补充对照线正式口径表", style_id="afd"))

    insert_after_block_prefix(
        blocks,
        "实验三用于分析固定负载条件下的能力边界与瓶颈位置。",
        [
            *make_figure_group(state, figure_map["fig_5_4"], "图B.4 实验三固定负载主曲线补充结果图"),
            *make_figure_group(state, figure_map["fig_5_5"], "图B.5 实验三峰值点补充结果图"),
        ],
    )
    replace_exact_block_text(blocks, "固定负载参数", make_caption(state, "表B.5 固定负载参数表", style_id="afd"))
    replace_exact_block_text(blocks, "后端线程拓扑", make_caption(state, "表B.6 后端线程拓扑表", style_id="afd"))
    replace_exact_block_text(blocks, "后端 CPU 绑定口径", make_caption(state, "表B.7 后端 CPU 绑定口径表", style_id="afd"))
    replace_exact_block_text(blocks, "实验三主曲线结果补充表", make_caption(state, "表B.8 实验三主曲线结果补充表", style_id="afd"))
    replace_exact_block_text(blocks, "实验三诊断指标补充表", make_caption(state, "表B.9 实验三诊断指标补充表", style_id="afd"))


def ensure_update_fields(settings_xml: str) -> str:
    if "<w:updateFields" in settings_xml:
        return settings_xml
    # 目录这次不手工把页码写死，而是打开“打开文档时更新域”。
    # 这样目录、图目录、表目录会在 Word 打开时按当前正文真实内容刷新。
    return settings_xml.replace("</w:settings>", '<w:updateFields w:val="true"/></w:settings>')


def next_relationship_id(document_rels_xml: str) -> int:
    ids = [int(match) for match in re.findall(r'Id="rId(\d+)"', document_rels_xml)]
    return max(ids) + 1


def build_figure_relationships(document_rels_xml: str, figure_png_dir: Path) -> tuple[str, dict[str, dict[str, str | int]], dict[str, bytes]]:
    next_rid = next_relationship_id(document_rels_xml)
    figure_map: dict[str, dict[str, str | int]] = {}
    media_payloads: dict[str, bytes] = {}
    new_relationships: list[str] = []

    for alias, filename in FIGURE_ALIAS_TO_FILE.items():
        png_path = figure_png_dir / filename
        if not png_path.exists():
            raise FileNotFoundError(f"缺少 PNG 图：{png_path}")

        width_px, height_px = read_png_size(png_path)
        cx, cy = scale_emu(width_px, height_px)
        rid = f"rId{next_rid}"
        next_rid += 1
        target_name = f"media/{alias}.png"
        figure_map[alias] = {
            "rid": rid,
            "cx": cx,
            "cy": cy,
            "name": alias,
            "target_name": target_name,
        }
        media_payloads[f"word/{target_name}"] = png_path.read_bytes()
        new_relationships.append(
            f'<Relationship Id="{rid}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="{target_name}"/>'
        )

    return (
        document_rels_xml.replace("</Relationships>", "".join(new_relationships) + "</Relationships>"),
        figure_map,
        media_payloads,
    )


def rebuild_document_xml(original_xml: str, state: XmlBuildState, figure_map: dict[str, dict[str, str | int]]) -> str:
    prefix, blocks, suffix = split_body_blocks(original_xml)
    apply_main_section_insertions(blocks, build_main_section_fragments(state, figure_map))
    apply_appendix_insertions(blocks, state, figure_map)
    return prefix + "".join(blocks) + suffix


def copy_docx_with_overrides(
    *,
    input_path: Path,
    output_path: Path,
    document_xml: str,
    settings_xml: str,
    document_rels_xml: str,
    extra_files: dict[str, bytes],
) -> None:
    with ZipFile(input_path, "r") as source, ZipFile(output_path, "w") as target:
        existing_names = {info.filename for info in source.infolist()}
        for info in source.infolist():
            if info.filename == "word/document.xml":
                data = document_xml.encode("utf-8")
            elif info.filename == "word/settings.xml":
                data = settings_xml.encode("utf-8")
            elif info.filename == "word/_rels/document.xml.rels":
                data = document_rels_xml.encode("utf-8")
            else:
                data = source.read(info.filename)

            clone = ZipInfo(filename=info.filename, date_time=info.date_time)
            clone.compress_type = info.compress_type
            clone.comment = info.comment
            clone.create_system = info.create_system
            clone.create_version = info.create_version
            clone.extract_version = info.extract_version
            clone.flag_bits = info.flag_bits
            clone.internal_attr = info.internal_attr
            clone.external_attr = info.external_attr
            target.writestr(clone, data)

        for extra_name, data in extra_files.items():
            if extra_name in existing_names:
                continue
            # 这里新增的只有 word/media 下的 PNG，不会去碰模板已有媒体资源。
            target.writestr(extra_name, data)


def validate_output_docx(path: Path) -> None:
    with ZipFile(path, "r") as archive:
        document_xml = archive.read("word/document.xml").decode("utf-8")
        settings_xml = archive.read("word/settings.xml").decode("utf-8")
        document_rels_xml = archive.read("word/_rels/document.xml.rels").decode("utf-8")

    root = ET.fromstring(document_xml)
    body = root.find("w:body", NS)
    if body is None:
        raise ValueError("输出文档缺少 w:body")

    text_list = [
        "".join(node.text or "" for node in child.findall(".//w:t", NS)).strip()
        for child in body
    ]
    expected_texts = [
        "图5.1 实验一生命周期保护正确性结果图",
        "图5.2 实验二主体对照线可见完成率结果图",
        "图5.3 实验二缓冲写入归因结果图",
        "图5.4 实验三固定负载主曲线结果图",
        "图5.5 实验三峰值点结果图",
        "表5.1 实验一生命周期保护正确性结果汇总",
        "表5.5 实验三峰值点结果汇总",
        "图B.1 实验一正确性补充结果图",
        "图B.5 实验三峰值点补充结果图",
        "表B.1 正式测试环境补充说明表",
        "表B.9 实验三诊断指标补充表",
    ]
    for expected in expected_texts:
        if expected not in text_list:
            raise ValueError(f"输出文档缺少预期内容：{expected}")

    if '<w:updateFields w:val="true"/>' not in settings_xml:
        raise ValueError("settings.xml 缺少 updateFields 开关")

    for alias in FIGURE_ALIAS_TO_FILE:
        if f'Target="media/{alias}.png"' not in document_rels_xml:
            raise ValueError(f"document.xml.rels 缺少图片关系：{alias}")


def main() -> None:
    args = parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output)
    figure_png_dir = Path(args.figure_png_dir)

    with ZipFile(input_path, "r") as archive:
        original_document_xml = archive.read("word/document.xml").decode("utf-8")
        original_settings_xml = archive.read("word/settings.xml").decode("utf-8")
        original_document_rels_xml = archive.read("word/_rels/document.xml.rels").decode("utf-8")

    updated_document_rels_xml, figure_map, media_payloads = build_figure_relationships(
        original_document_rels_xml,
        figure_png_dir,
    )
    state = XmlBuildState()
    rebuilt_document_xml = rebuild_document_xml(original_document_xml, state, figure_map)
    updated_settings_xml = ensure_update_fields(original_settings_xml)
    copy_docx_with_overrides(
        input_path=input_path,
        output_path=output_path,
        document_xml=rebuilt_document_xml,
        settings_xml=updated_settings_xml,
        document_rels_xml=updated_document_rels_xml,
        extra_files=media_payloads,
    )
    validate_output_docx(output_path)
    print(f"wrote {output_path}")


if __name__ == "__main__":
    main()
