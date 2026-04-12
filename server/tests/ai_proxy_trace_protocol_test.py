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

    def test_glm_provider_analyze_trace_uses_json_object_and_extracts_usage(self):
        project_root = pathlib.Path(__file__).resolve().parents[1]
        if str(project_root) not in sys.path:
            sys.path.insert(0, str(project_root))
        module = importlib.import_module("ai.proxy.providers.glm")

        captured = {}

        class FakeResponse:
            status_code = 200

            def raise_for_status(self):
                return None

            def json(self):
                return {
                    "choices": [
                        {
                            "message": {
                                "content": "{\"summary\":\"ok\",\"risk_level\":\"info\",\"root_cause\":\"none\",\"solution\":\"none\"}"
                            }
                        }
                    ],
                    "usage": {
                        "prompt_tokens": 11,
                        "completion_tokens": 7,
                        "total_tokens": 18,
                    },
                }

        class FakeClient:
            def __init__(self, *args, **kwargs):
                # 这里锁的是 provider 走同步 httpx.Client，而不是偷偷换成别的 HTTP 栈。
                captured["client_kwargs"] = kwargs

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc, tb):
                return False

            def post(self, url, *, headers=None, json=None):
                # 这里直接抓最终出站请求，避免测试只盯着本地临时变量而看不到真实 HTTP 载荷。
                captured["url"] = url
                captured["headers"] = headers
                captured["json"] = json
                return FakeResponse()

        from unittest import mock
        with mock.patch.object(module.httpx, "Client", FakeClient):
            provider = module.GlmProvider(api_key="", model_name="glm-5.1")
            result = provider.analyze_trace(
                trace_text="trace body should not be duplicated",
                prompt="rendered trace prompt",
                api_key="glm-key",
                model="glm-test",
            )

        self.assertTrue(result["ok"])
        self.assertEqual(result["analysis"]["summary"], "ok")
        self.assertEqual(result["usage"]["input_tokens"], 11)
        self.assertEqual(result["usage"]["output_tokens"], 7)
        self.assertEqual(result["usage"]["total_tokens"], 18)
        self.assertEqual(captured["url"], "https://open.bigmodel.cn/api/paas/v4/chat/completions")
        self.assertEqual(captured["headers"]["Authorization"], "Bearer glm-key")
        self.assertEqual(captured["json"]["model"], "glm-test")
        self.assertEqual(captured["json"]["response_format"], {"type": "json_object"})
        self.assertEqual(captured["json"]["messages"][0]["role"], "user")
        self.assertEqual(captured["json"]["messages"][0]["content"], "rendered trace prompt")

    def test_glm_provider_analyze_trace_rejects_invalid_json_content(self):
        project_root = pathlib.Path(__file__).resolve().parents[1]
        if str(project_root) not in sys.path:
            sys.path.insert(0, str(project_root))
        module = importlib.import_module("ai.proxy.providers.glm")

        class FakeResponse:
            status_code = 200

            def raise_for_status(self):
                return None

            def json(self):
                return {
                    "choices": [
                        {
                            "message": {
                                "content": "not-a-json-object"
                            }
                        }
                    ]
                }

        class FakeClient:
            def __init__(self, *args, **kwargs):
                pass

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc, tb):
                return False

            def post(self, url, *, headers=None, json=None):
                return FakeResponse()

        from unittest import mock
        with mock.patch.object(module.httpx, "Client", FakeClient):
            provider = module.GlmProvider(api_key="", model_name="glm-5.1")
            result = provider.analyze_trace(
                trace_text="trace text",
                prompt="rendered trace prompt",
                api_key="glm-key",
                model="glm-test",
            )

        # 既然 GLM 这里只能保证 JSON mode，不能保证服务端 schema 强校验，
        # 那么 provider 就必须自己兜底：无效 JSON 只能算 provider 格式失败，不能伪造成成功 analysis。
        self.assertFalse(result["ok"])
        self.assertEqual(result["error_status"], "PROVIDER_FORMAT_ERROR")
        self.assertIn("JSON", result["error_message"])


if __name__ == "__main__":
    unittest.main()
