#!/usr/bin/env python3
import importlib.util
import importlib
import pathlib
import sys
import unittest


def load_module(module_name: str, relative_path: str):
    script_path = pathlib.Path(__file__).resolve().parents[1] / relative_path
    spec = importlib.util.spec_from_file_location(module_name, script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load module from {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AiProxyTraceProtocolTest(unittest.TestCase):
    def test_normalize_trace_success_payload_keeps_analysis_and_usage(self):
        module = load_module("ai_proxy_main", "ai/proxy/main.py")

        normalized = module.normalize_trace_result(
            "gemini",
            {
                "ok": True,
                "analysis": {
                    "summary": "trace ok",
                    "risk_level": "info",
                    "root_cause": "none",
                    "solution": "none",
                },
                "usage": {"total_tokens": 15},
            },
        )

        # 这里锁的是 proxy 对 C++ 的成功外部契约。
        # 一旦 provider 已经给出统一结构，路由层只允许补 provider，不应该再改 analysis/usage 语义。
        self.assertTrue(normalized["ok"])
        self.assertEqual(normalized["provider"], "gemini")
        self.assertEqual(normalized["analysis"]["summary"], "trace ok")
        self.assertEqual(normalized["usage"]["total_tokens"], 15)

    def test_normalize_trace_failure_payload_keeps_error_fields(self):
        module = load_module("ai_proxy_main", "ai/proxy/main.py")

        normalized = module.normalize_trace_result(
            "gemini",
            {
                "ok": False,
                "error_code": 429,
                "error_status": "RESOURCE_EXHAUSTED",
                "error_message": "quota exhausted",
            },
        )

        # 这里锁的是“失败也必须走统一 JSON 协议”，
        # 因为后面的 C++ 熔断只应该读 body 字段，不应该再去拆 HTTP 500 文本。
        self.assertFalse(normalized["ok"])
        self.assertEqual(normalized["provider"], "gemini")
        self.assertEqual(normalized["error_code"], 429)
        self.assertEqual(normalized["error_status"], "RESOURCE_EXHAUSTED")
        self.assertEqual(normalized["error_message"], "quota exhausted")

    def test_gemini_provider_extracts_structured_error_fields(self):
        project_root = pathlib.Path(__file__).resolve().parents[1]
        if str(project_root) not in sys.path:
            sys.path.insert(0, str(project_root))
        module = importlib.import_module("ai.proxy.providers.gemini")

        provider = module.GeminiProvider(api_key="", model_name="gemini-test")

        # 这里模拟 SDK 已经把 HTTP 错误拆成 code/status/message。
        # provider 的职责不是把它吞掉伪造成成功 analysis，而是继续转成统一失败载荷。
        api_error = Exception("raw fallback")
        api_error.code = 429
        api_error.status = "RESOURCE_EXHAUSTED"
        api_error.message = "quota exhausted"

        error_payload = provider._build_trace_error_payload(api_error)

        self.assertFalse(error_payload["ok"])
        self.assertEqual(error_payload["error_code"], 429)
        self.assertEqual(error_payload["error_status"], "RESOURCE_EXHAUSTED")
        self.assertEqual(error_payload["error_message"], "quota exhausted")


if __name__ == "__main__":
    unittest.main()
