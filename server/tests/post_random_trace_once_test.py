#!/usr/bin/env python3
import importlib.util
import json
import pathlib
import unittest


def load_module():
    script_path = pathlib.Path(__file__).with_name("post_random_trace_once.py")
    spec = importlib.util.spec_from_file_location("post_random_trace_once", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load module from {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PostRandomTraceOnceTest(unittest.TestCase):
    def test_build_complex_trace_can_force_critical_marker(self):
        module = load_module()

        # 这里锁的是“脚本本身能不能稳定产出 critical 语义”，不是网络请求。
        # 只要生成的 trace JSON 里能明确看到 critical 字样，mock trace AI 就能稳定命中 critical 分支。
        spans = module.build_complex_trace(123456789, 1710000000000, force_critical=True)
        payload_text = json.dumps(spans, ensure_ascii=False).lower()

        self.assertIn("critical", payload_text)


if __name__ == "__main__":
    unittest.main()
