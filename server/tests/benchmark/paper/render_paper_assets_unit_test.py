#!/usr/bin/env python3

import csv
import io
import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    import render_paper_assets as render_module
except ModuleNotFoundError:
    render_module = None


def add_json_member(archive: tarfile.TarFile, member_name: str, payload) -> None:
    encoded = json.dumps(payload).encode("utf-8")
    info = tarfile.TarInfo(member_name)
    info.size = len(encoded)
    archive.addfile(info, io.BytesIO(encoded))


def write_fixture_archive(path: Path, members) -> None:
    # 这些 fixture 只保留真实 summary 里画论文图需要的字段，
    # 避免单测绑定完整远端目录和大体积 result.json。
    with tarfile.open(path, "w:gz") as archive:
        for member_name, payload in members.items():
            add_json_member(archive, member_name, payload)


class RenderPaperAssetsUnitTest(unittest.TestCase):
    def test_default_discovery_excludes_old_suite_d_archive(self) -> None:
        if render_module is None or not hasattr(render_module, "discover_default_archives"):
            self.fail("discover_default_archives should exist for paper asset renderer")

        with tempfile.TemporaryDirectory() as temp_dir:
            asset_dir = Path(temp_dir)
            for name in [
                "suite_a_20260418.tar.gz",
                "suite_b_20260418_x10.tar.gz",
                "suite_d_20260418.tar.gz",
                "suite_d_final_curve_rerun_d512_f1024.tar.gz",
                "suite_d_final_peak_20c_d512_f1024.tar.gz",
            ]:
                (asset_dir / name).write_text("placeholder", encoding="utf-8")

            archives = render_module.discover_default_archives(asset_dir)

        self.assertEqual(
            [
                "suite_a_20260418.tar.gz",
                "suite_b_20260418_x10.tar.gz",
                "suite_d_final_curve_rerun_d512_f1024.tar.gz",
                "suite_d_final_peak_20c_d512_f1024.tar.gz",
            ],
            [path.name for path in archives],
        )

    def test_render_assets_writes_csv_tex_and_svg_for_four_archives(self) -> None:
        if render_module is None or not hasattr(render_module, "render_assets"):
            self.fail("render_assets should exist for paper asset renderer")

        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            asset_dir = root / "paper_assets"
            output_dir = root / "rendered"
            asset_dir.mkdir()
            write_fixture_archive(
                asset_dir / "suite_a_20260418.tar.gz",
                {
                    "suite_a/summary/main_vs_cmp_summary.json": {
                        "by_load": [
                            {
                                "label": "light",
                                "variants": {
                                    "cmp_baseline": {
                                        "visible_completion_rate_at_stop": 1.0,
                                        "drain_tail_ms": 1.0,
                                    },
                                    "main_tuned": {
                                        "visible_completion_rate_at_stop": 0.99,
                                        "drain_tail_ms": 10.0,
                                    },
                                },
                                "delta_main_vs_cmp": {
                                    "visible_completion_rate_at_stop": -0.01,
                                    "drain_tail_ms": 9.0,
                                },
                            }
                        ],
                    },
                    "suite_a/summary/buffer_compare_summary.json": {
                        "top_candidates": [
                            {
                                "candidate_name": "buffered_512_5",
                                "visible_completion_rate_at_stop": 0.998,
                                "drain_tail_ms": 120.0,
                            }
                        ]
                    },
                },
            )
            write_fixture_archive(
                asset_dir / "suite_b_20260418_x10.tar.gz",
                {
                    "suite_b/summary/campaign_summary_x10.json": {
                        "aggregate": {
                            "correctness_by_case": {
                                "minimal__clean_baseline": {
                                    "trace_completeness_rate": {"median": 0.88, "min": 0.85},
                                    "trace_pollution_rate": {"median": 0.0},
                                    "duplicate_persistence_rate": {"median": 0.0},
                                },
                                "minimal__late_replay_stress": {
                                    "trace_completeness_rate": {"median": 0.08, "min": 0.07},
                                    "trace_pollution_rate": {"median": 0.14},
                                    "duplicate_persistence_rate": {"median": 0.0},
                                },
                                "minimal__mixed_realistic": {
                                    "trace_completeness_rate": {"median": 0.12, "min": 0.03},
                                    "trace_pollution_rate": {"median": 0.09},
                                    "duplicate_persistence_rate": {"median": 0.0},
                                },
                                "protected__clean_baseline": {
                                    "trace_completeness_rate": {"median": 1.0, "min": 1.0},
                                    "trace_pollution_rate": {"median": 0.0},
                                    "duplicate_persistence_rate": {"median": 0.0},
                                },
                                "protected__late_replay_stress": {
                                    "trace_completeness_rate": {"median": 1.0, "min": 0.98},
                                    "trace_pollution_rate": {"median": 0.0},
                                    "duplicate_persistence_rate": {"median": 0.0},
                                },
                                "protected__mixed_realistic": {
                                    "trace_completeness_rate": {"median": 1.0, "min": 0.99},
                                    "trace_pollution_rate": {"median": 0.0},
                                    "duplicate_persistence_rate": {"median": 0.0},
                                }
                            },
                            "sqlite_unique_constraint_fail_count_by_case": {
                                "minimal__clean_baseline": {"median": 0.0},
                                "minimal__late_replay_stress": {"median": 12.0},
                                "minimal__mixed_realistic": {"median": 11.0},
                                "protected__clean_baseline": {"median": 0.0},
                                "protected__late_replay_stress": {"median": 0.0},
                                "protected__mixed_realistic": {"median": 0.0}
                            },
                        }
                    }
                },
            )
            write_fixture_archive(
                asset_dir / "suite_d_final_curve_rerun_d512_f1024.tar.gz",
                {
                    "final_curve_rerun_d512_f1024/summary/scaling_summary.json": {
                        "by_backend_cores": [
                            {
                                "backend_cores": 4,
                                "online_completed_traces_per_sec": 3200.0,
                                "online_completion_ratio": 0.65,
                                "drain_tail_ms": 410,
                                "wrk_metrics": {"requests_per_sec": 40000.0},
                            }
                        ],
                        "overall": {"best_backend_cores": 4},
                    }
                },
            )
            write_fixture_archive(
                asset_dir / "suite_d_final_peak_20c_d512_f1024.tar.gz",
                {
                    "final_peak_20c_d512_f1024/summary/scaling_summary.json": {
                        "by_backend_cores": [
                            {
                                "backend_cores": 20,
                                "online_completed_traces_per_sec": 12597.8,
                                "online_completion_ratio": 0.6067,
                                "drain_tail_ms": 3189,
                                "wrk_metrics": {"requests_per_sec": 165019.67},
                            }
                        ],
                        "overall": {"best_backend_cores": 20},
                    }
                },
            )
            (asset_dir / "suite_d_20260418.tar.gz").write_text("old", encoding="utf-8")

            args = render_module.parse_args(["--asset-dir", str(asset_dir), "--output-dir", str(output_dir)])
            manifest = render_module.render_assets(args)

            main_csv = output_dir / "paper_tables" / "suite_a_main_vs_cmp.csv"
            with main_csv.open("r", encoding="utf-8", newline="") as fh:
                rows = list(csv.DictReader(fh))
            suite_b_tex_exists = (output_dir / "paper_tables" / "suite_b_correctness.tex").exists()
            suite_d_curve_svg_exists = (output_dir / "paper_figures" / "suite_d_fixed_load_curve.svg").exists()
            suite_d_peak_svg_exists = (output_dir / "paper_figures" / "suite_d_peak.svg").exists()
            suite_b_svg = (output_dir / "paper_figures" / "suite_b_correctness.svg").read_text(encoding="utf-8")

        self.assertEqual(4, len(manifest["archives"]))
        self.assertNotIn("suite_d_20260418.tar.gz", manifest["archives"])
        self.assertEqual("light", rows[0]["load"])
        self.assertEqual("0.9900", rows[0]["main_visible_completion_rate_at_stop"])
        self.assertTrue(suite_b_tex_exists)
        self.assertTrue(suite_d_curve_svg_exists)
        self.assertTrue(suite_d_peak_svg_exists)
        # Suite B 主图回到竖向柱图后，只保留 completeness，
        # 长 case 名必须缩成短标签，0/1 极值则要明确贴成 0.00 / 1.00。
        self.assertNotIn("protected__mixed_realistic", suite_b_svg)
        self.assertNotIn(">pollution<", suite_b_svg)
        self.assertIn(">M-LR<", suite_b_svg)
        self.assertIn(">P-MR<", suite_b_svg)
        self.assertIn(">0.88<", suite_b_svg)
        self.assertIn(">0.08<", suite_b_svg)
        self.assertIn(">0.12<", suite_b_svg)
        self.assertIn(">1.00<", suite_b_svg)
        self.assertIn(">0.00<", suite_b_svg)


if __name__ == "__main__":
    unittest.main()
