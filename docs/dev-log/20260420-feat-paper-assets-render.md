# Git Commit Message

feat(benchmark): 生成论文资产表格与图表

# Modification

- `server/tests/benchmark/paper/render_paper_assets.py`
- `server/tests/benchmark/paper/render_paper_assets_unit_test.py`
- `server/tests/benchmark/paper_assets/rendered/manifest.json`
- `server/tests/benchmark/paper_assets/rendered/paper_tables/*.csv`
- `server/tests/benchmark/paper_assets/rendered/paper_tables/*.tex`
- `server/tests/benchmark/paper_assets/rendered/paper_figures/*.svg`
- `docs/todo-list/Todo_Benchmark.md`
- `docs/dev-log/20260420-feat-paper-assets-render.md`

# Learning Tips

## Newbie Tips

- 论文图表不要手抄 JSON。既然 summary 已经是结构化数据，那么应该让脚本把固定字段抽成 CSV/LaTeX/SVG，后续改图样式时只重跑脚本，不重新拷数字。
- 旧实验资产不要和正式口径混用。这次默认排除 `suite_d_20260418.tar.gz`，只读取 4 个正式资产包，避免把旧 scaling 口径误放进正文。

## Function Explanation

- `tarfile.open(..., "r:gz")`：直接读取 `.tar.gz` 压缩包，不需要先完整解压目录。
- `archive.extractfile(member)`：只从压缩包里取指定 summary JSON，适合这种“小字段抽表”的场景。
- `csv.DictWriter`：按固定表头写 CSV，字段顺序稳定，方便论文工具或电子表格继续处理。

## Pitfalls

- 不要把 `result.json/server.log` 全量解压后再画图；这些文件只是复核资产，论文主图应优先读 suite 级 `summary/*.json`。
- 不要依赖 matplotlib 这类本机未必安装的库生成基础图。这里用内置 SVG 生成器，牺牲一点精美度，换来脚本在云机/本机都能直接跑。
- 不要把临时目录生命周期外的路径拿去做单测断言；`TemporaryDirectory` 退出后文件会被删除，这次单测把存在性判断放在目录仍存在时完成。
- 不要把完整 case_id 直接塞进正文图的 x 轴。`Suite B` 这种 `profile + sender scenario` 组合名太长，应该先缩成 `M-CB / P-LR` 这类短标签，再把完整解释放到图下注释。
- 0 值不是“没东西可画”，而是论文结论本身。像污染率 `0.00` 这种点位必须明确渲染文本，不能因为柱长为 0 就让读者自己猜。

# Verification

- `python3 -m unittest server.tests.benchmark.paper.render_paper_assets_unit_test`
- `python3 -m py_compile server/tests/benchmark/paper/render_paper_assets.py server/tests/benchmark/paper/render_paper_assets_unit_test.py`
- `python3 server/tests/benchmark/paper/render_paper_assets.py`
- `git diff --check`

# Follow-up Fix

- 修正 `suite_b_correctness.svg`：
  - 改回竖向柱图，只保留正文主指标 `completeness`；
  - 把完整 `minimal__late_replay_stress` 改成 `M-LR` 这类短标签，并在底部补缩写解释；
  - `0.00 / 1.00` 这类极值显式贴值，避免关键点位被忽略；
  - `pollution / duplicate / unique constraint` 继续保留在 CSV / TeX 表，不再和主图抢空间。
  - 后续又补一刀：既然主图现在只剩 6 根柱，那么 `0.88 / 0.08 / 0.12 / 1.00` 这些值都直接贴出来，避免左侧 `M` 组只能靠肉眼估读。

---

# Git Commit Message

feat(paper): 生成论文正文表格安全版文档

# Modification

- `server/tests/benchmark/paper/integrate_tables_into_thesis_docx.py`
- `docs/todo-list/Todo_ThesisDocx.md`
- `docs/dev-log/20260420-feat-paper-assets-render.md`
- `毕设论文初稿草稿_正文表格安全版.docx`

# Learning Tips

## Newbie Tips

- `.docx` 不是“一个文件”，本质上是一个 ZIP 包。只要你把 `word/document.xml` 根标签上的命名空间声明弄丢，Word 很可能就会弹“发现无法读取的内容”，哪怕正文文字看起来没坏。
- 这种带强模板约束的论文文档，最稳的做法不是“解析整棵树再写回去”，而是保留原始 `document.xml` 头尾，只在 `w:body` 顶层块之间做最小插入。
- 正文和附录的分工要分清。正文应该先给主结果表和“见表”引用，附录再承接参数口径、诊断量和补充说明，不然正文证据链会发虚。

## Function Explanation

- `zipfile.ZipFile`：按 OOXML 包结构逐项复制原始条目，只替换 `word/document.xml`，适合这种“保持模板不动，只改正文内容”的场景。
- `xml.etree.ElementTree.fromstring`：这里不是拿来重写整份 XML，而只是把单个片段包进临时根节点后做只读解析，用来识别锚点段落文本。
- `xml.sax.saxutils.escape`：把正文里的 `&`、`<` 之类字符安全转义，避免手写 XML 片段时把文档结构直接写坏。

## Pitfalls

- 不要在独立 XML 片段上直接 `ET.fromstring(fragment)`。片段里常带 `w14:`、`r:` 这类前缀，单独解析时没有命名空间声明，会直接报 `unbound prefix`。
- 不要为了“改起来方便”把 `word/document.xml` 全量序列化回写。你一旦让库重排了根标签，兼容命名空间和 `mc:Ignorable` 对应关系就可能被破坏。
- 不要把实验 CSV 原字段名原封不动塞进正文表头。像 `visible_completion_rate_at_stop` 这种字段在论文正文里可读性太差，应该先转成短中文表头，再把完整术语留在附录或正文说明里。

# Verification

- `python3 -m py_compile server/tests/benchmark/paper/integrate_tables_into_thesis_docx.py`
- `python3 server/tests/benchmark/paper/integrate_tables_into_thesis_docx.py`
- `unzip -t 毕设论文初稿草稿_正文表格安全版.docx`
- 额外结构检查：确认原始 `word/document.xml` 根标签与输出文件完全一致，且表 `5.1` 到表 `5.5` 已插入到 `5.2.3 / 5.3.3 / 5.4.3` 之前
