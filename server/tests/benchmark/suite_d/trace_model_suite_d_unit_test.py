#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


SUITE_D_DIR = Path(__file__).resolve().parent
TRACE_MODEL = SUITE_D_DIR / "trace_model_suite_d.lua"


class TraceModelSuiteDUnitTest(unittest.TestCase):
    def test_wrk_thread_id_uses_global_injection_without_local_shadowing(self) -> None:
        content = TRACE_MODEL.read_text(encoding="utf-8")

        # wrk 的 thread:set 会把 thread_id 注入到每个工作线程的全局表里；
        # 如果 Lua 文件再声明同名 local，init/request 读到的就是本地 0，
        # 多个 wrk 线程会落到同一段 trace_key，最终触发 SQLite UNIQUE 冲突。
        self.assertNotRegex(content, re.compile(r"^\s*local\s+thread_id\s*=", re.MULTILINE))
        self.assertIn('thread:set("thread_id", setup_counter)', content)
        self.assertRegex(content, re.compile(r"wrk_thread_id\s*=\s*_G\.thread_id\s+or\s+0"))
        self.assertIn("next_trace_key = (wrk_thread_id + 1) * 1000000000", content)
        self.assertIn('thread_id = tostring(wrk_thread_id)', content)


if __name__ == "__main__":
    unittest.main()
