#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path
from zipfile import ZipFile, ZipInfo


MEDIA_REPLACEMENTS = {
    # 图 3.1 和图 3.2 在当前论文成品里已经绑定好了题注、目录和页码。
    # 这里故意只替换 media 文件本身，不去重写 document.xml，
    # 这样可以最大限度保留 Word 模板结构，避免再引入打不开或弹修复框的问题。
    "word/media/image3.png": Path("docs/paper_figures/png/3_2_system_architecture.png"),
    "word/media/image4.png": Path("docs/paper_figures/png/3_3_main_business_flow.png"),
}

EXPECTED_CAPTION_KEYWORDS = [
    "LogSentinel 系统总体架构图",
    "LogSentinel 系统主业务流程图",
]

FIGURE_SIZE_OVERRIDES = {
    # rId26 对应图 3.2。
    # 这张主业务流程图现在改成 Mermaid 竖向总览图，如果继续沿用旧的横版显示尺寸，
    # Word 会把内容压得过窄。因此这里只改这个图块的 inline extent，不碰其他正文结构。
    "rId26": ("4000000", "5540000"),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="仅替换论文中第 3 章两张总图对应的 media 资源。")
    parser.add_argument("--input", default="毕业论文初稿_3.2-4.5终检版.docx", help="输入 docx 路径")
    parser.add_argument(
        "--output",
        default="毕业论文初稿_3.2-4.5终检版_mmd图替换版.docx",
        help="输出 docx 路径",
    )
    return parser.parse_args()


def ensure_inputs_exist(input_path: Path) -> None:
    if not input_path.exists():
        raise FileNotFoundError(f"输入论文不存在：{input_path}")
    for media_name, png_path in MEDIA_REPLACEMENTS.items():
        if not png_path.exists():
            raise FileNotFoundError(f"缺少替换图片：{media_name} <- {png_path}")


def replace_media_only(input_path: Path, output_path: Path) -> None:
    with ZipFile(input_path, "r") as source, ZipFile(output_path, "w") as target:
        existing_names = {info.filename for info in source.infolist()}
        missing_media = [name for name in MEDIA_REPLACEMENTS if name not in existing_names]
        if missing_media:
            raise FileNotFoundError("输入论文缺少目标 media: " + ", ".join(missing_media))

        for info in source.infolist():
            if info.filename == "word/document.xml":
                data = rewrite_document_xml_for_figure_size(source.read(info.filename).decode("utf-8", errors="ignore")).encode("utf-8")
            elif info.filename in MEDIA_REPLACEMENTS:
                data = MEDIA_REPLACEMENTS[info.filename].read_bytes()
            else:
                data = source.read(info.filename)

            # 保留原 zip 条目的压缩方式和时间戳，尽量避免 Word 侧看到异常变化。
            clone = ZipInfo(filename=info.filename, date_time=info.date_time)
            clone.compress_type = info.compress_type
            clone.comment = info.comment
            clone.create_system = info.create_system
            clone.create_version = info.create_version
            clone.extract_version = info.extract_version
            clone.flag_bits = info.flag_bits
            clone.volume = info.volume
            clone.internal_attr = info.internal_attr
            clone.external_attr = info.external_attr
            target.writestr(clone, data)


def rewrite_document_xml_for_figure_size(document_xml: str) -> str:
    updated_xml = document_xml
    for rid, (cx, cy) in FIGURE_SIZE_OVERRIDES.items():
        # 这里只允许命中“单个 w:drawing 块内部已经包含目标 rId”的图块。
        # 如果直接用 .*? 从第一个 <w:drawing> 扫到后面的 rId，很容易跨块误伤封面图。
        pattern = re.compile(
            rf"(<w:drawing>(?:(?!</w:drawing>).)*?r:embed=\"{rid}\"(?:(?!</w:drawing>).)*?</w:drawing>)",
            re.DOTALL,
        )
        match = pattern.search(updated_xml)
        if match is None:
            raise ValueError(f"document.xml 中未找到目标图块：{rid}")

        drawing_xml = match.group(1)
        drawing_xml = re.sub(r'<wp:extent cx="\d+" cy="\d+"', f'<wp:extent cx="{cx}" cy="{cy}"', drawing_xml, count=1)
        drawing_xml = re.sub(r'<a:ext cx="\d+" cy="\d+"', f'<a:ext cx="{cx}" cy="{cy}"', drawing_xml, count=1)
        updated_xml = updated_xml[: match.start(1)] + drawing_xml + updated_xml[match.end(1) :]

    return updated_xml


def validate_output(output_path: Path) -> None:
    with ZipFile(output_path, "r") as archive:
        document_xml = archive.read("word/document.xml").decode("utf-8", errors="ignore")
        bad_file = archive.testzip()

        for media_name in MEDIA_REPLACEMENTS:
            if media_name not in archive.namelist():
                raise ValueError(f"输出论文缺少 media：{media_name}")

    if bad_file is not None:
        raise ValueError(f"输出论文压缩包损坏：{bad_file}")

    plain_text = re.sub(r"</w:p>", "\n", document_xml)
    plain_text = re.sub(r"<[^>]+>", "", plain_text)

    for caption_keyword in EXPECTED_CAPTION_KEYWORDS:
        if caption_keyword not in plain_text:
            raise ValueError(f"输出论文缺少题注关键词：{caption_keyword}")

    for rid, (cx, cy) in FIGURE_SIZE_OVERRIDES.items():
        if rid not in document_xml:
            raise ValueError(f"输出论文缺少目标图块：{rid}")
        if f'<wp:extent cx="{cx}" cy="{cy}"' not in document_xml:
            raise ValueError(f"输出论文未写入 wp:extent 尺寸：{rid}")
        if f'<a:ext cx="{cx}" cy="{cy}"' not in document_xml:
            raise ValueError(f"输出论文未写入 a:ext 尺寸：{rid}")


def main() -> None:
    args = parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output)

    ensure_inputs_exist(input_path)
    replace_media_only(input_path, output_path)
    validate_output(output_path)
    print(f"wrote {output_path}")


if __name__ == "__main__":
    main()
