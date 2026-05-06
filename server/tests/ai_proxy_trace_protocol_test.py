#!/usr/bin/env python3
import asyncio
import importlib.util
import importlib
import json
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
    def test_parse_proxy_args_reads_max_workers_from_cli(self):
        module = load_module("ai_proxy_main_cli_args", "ai/proxy/main.py")

        args = module.parse_proxy_args(["--host", "0.0.0.0", "--port", "9001", "--max-workers", "256"])

        # 这里锁 CLI 口径，而不是环境变量。
        # benchmark 命令要能直接写在论文和脚本里，所以 max-workers 必须是显式启动参数。
        self.assertEqual(args.host, "0.0.0.0")
        self.assertEqual(args.port, 9001)
        self.assertEqual(args.max_workers, 256)

    def test_configure_default_thread_limiter_updates_anyio_capacity(self):
        module = load_module("ai_proxy_main_limiter_config", "ai/proxy/main.py")

        async def run():
            import anyio.to_thread

            limiter = anyio.to_thread.current_default_thread_limiter()
            original_tokens = limiter.total_tokens
            try:
                # 这里直接锁“启动期配置 helper 真会改 AnyIO 默认 limiter”，
                # 否则 `--max-workers` 只是打印到了日志里，proxy 实际并发上限还是旧值。
                await module.configure_default_thread_limiter(96)
                self.assertEqual(limiter.total_tokens, 96)
            finally:
                limiter.total_tokens = original_tokens

        asyncio.run(run())

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

    def test_deepseek_provider_analyze_trace_uses_openai_compatible_json_object_and_usage(self):
        project_root = pathlib.Path(__file__).resolve().parents[1]
        if str(project_root) not in sys.path:
            sys.path.insert(0, str(project_root))
        module = importlib.import_module("ai.proxy.providers.deepseek")

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
                                "content": "{\"summary\":\"deepseek ok\",\"risk_level\":\"warning\",\"root_cause\":\"slow db\",\"solution\":\"add index\"}"
                            }
                        }
                    ],
                    "usage": {
                        "prompt_tokens": 21,
                        "completion_tokens": 9,
                        "total_tokens": 30,
                    },
                }

        class FakeClient:
            def __init__(self, *args, **kwargs):
                # 这里锁 provider 仍然使用同步 httpx.Client。
                # 路由层已经负责把阻塞调用丢进线程池，provider 自己不要再混一套 async 生命周期。
                captured["client_kwargs"] = kwargs

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc, tb):
                return False

            def post(self, url, *, headers=None, json=None):
                # 这里直接抓 DeepSeek 出站 HTTP 载荷。
                # 既然 DeepSeek 官方是 OpenAI-compatible，这个测试要锁住 base_url、Bearer key 和 JSON mode。
                captured["url"] = url
                captured["headers"] = headers
                captured["json"] = json
                return FakeResponse()

        from unittest import mock
        with mock.patch.object(module.httpx, "Client", FakeClient):
            provider = module.DeepSeekProvider(api_key="", model_name="deepseek-v4-flash")
            result = provider.analyze_trace(
                trace_text="trace body should not be duplicated",
                prompt="rendered trace prompt with json instruction",
                api_key="deepseek-key",
                model="deepseek-v4-pro",
            )

        self.assertTrue(result["ok"])
        self.assertEqual(result["analysis"]["summary"], "deepseek ok")
        self.assertEqual(result["usage"]["input_tokens"], 21)
        self.assertEqual(result["usage"]["output_tokens"], 9)
        self.assertEqual(result["usage"]["total_tokens"], 30)
        self.assertEqual(captured["url"], "https://api.deepseek.com/chat/completions")
        self.assertEqual(captured["headers"]["Authorization"], "Bearer deepseek-key")
        self.assertEqual(captured["json"]["model"], "deepseek-v4-pro")
        self.assertEqual(captured["json"]["response_format"], {"type": "json_object"})
        self.assertEqual(captured["json"]["messages"][0]["role"], "user")
        self.assertEqual(captured["json"]["messages"][0]["content"], "rendered trace prompt with json instruction")

    def test_trace_route_passes_timeout_ms_to_provider(self):
        module = load_module("ai_proxy_main_timeout", "ai/proxy/main.py")
        captured = {}

        class FakeRequest:
            def __init__(self, body: bytes):
                self._body = body
                self.headers = {"content-type": "application/json"}

            async def body(self):
                return self._body

        class FakeProvider:
            def analyze_trace(self, *args, **kwargs):
                raise AssertionError("should not execute real provider body in this test")

        async def fake_call_provider_in_threadpool(func, *args, **kwargs):
            # 这里直接抓路由真正准备下发给 provider 的 kwargs，
            # 避免再把测试绑到线程池实现细节上，导致红灯原因被 anyio/线程调度噪音污染。
            captured["func"] = func
            captured["kwargs"] = kwargs
            captured["trace_text"] = kwargs["trace_text"]
            captured["timeout_ms"] = kwargs["timeout_ms"]
            captured["prompt"] = kwargs["prompt"]
            captured["model"] = kwargs["model"]
            captured["api_key"] = kwargs["api_key"]
            return {
                "ok": True,
                "analysis": {
                    "summary": "ok",
                    "risk_level": "info",
                    "root_cause": "none",
                    "solution": "none",
                },
                "usage": None,
            }

        original_provider = module.providers["glm"]
        original_call = module.call_provider_in_threadpool
        module.providers["glm"] = FakeProvider()
        module.call_provider_in_threadpool = fake_call_provider_in_threadpool
        try:
            response = asyncio.run(
                module.analyze_trace(
                    "glm",
                    FakeRequest(
                        json.dumps(
                            {
                                "trace_text": "trace body",
                                "prompt": "prompt body",
                                "model": "glm-test",
                                "api_key": "glm-key",
                                "timeout_ms": 30000,
                            }
                        ).encode("utf-8")
                    ),
                )
            )
        finally:
            module.providers["glm"] = original_provider
            module.call_provider_in_threadpool = original_call

        self.assertTrue(response["ok"])
        self.assertEqual(captured["trace_text"], "trace body")
        self.assertEqual(captured["prompt"], "prompt body\n\n<trace_context>\ntrace body\n</trace_context>")
        self.assertEqual(captured["model"], "glm-test")
        self.assertEqual(captured["api_key"], "glm-key")
        self.assertEqual(captured["timeout_ms"], 30000)

    def test_trace_route_passes_retry_config_to_retry_executor(self):
        module = load_module("ai_proxy_main_retry_route", "ai/proxy/main.py")
        captured = {}

        class FakeRequest:
            def __init__(self, body: bytes):
                self._body = body
                self.headers = {"content-type": "application/json"}

            async def body(self):
                return self._body

        class FakeProvider:
            def analyze_trace(self, *args, **kwargs):
                raise AssertionError("route should go through retry executor instead of calling provider directly")

        async def fail_if_called(*args, **kwargs):
            raise AssertionError("route should delegate to retry executor before touching threadpool bridge")

        async def fake_execute_trace_provider_with_retry(provider, **kwargs):
            # 这里锁的是“路由层必须把 retry 配置交给统一重试执行器”，
            # 否则后面就算实现了 should_retry，Settings 里的 ai_retry_* 也永远进不了真实主链。
            captured["provider"] = provider
            captured["kwargs"] = kwargs
            return {
                "ok": True,
                "analysis": {
                    "summary": "ok",
                    "risk_level": "info",
                    "root_cause": "none",
                    "solution": "none",
                },
                "usage": None,
            }

        fake_provider = FakeProvider()
        original_provider = module.providers["glm"]
        original_call = module.call_provider_in_threadpool
        original_execute = getattr(module, "execute_trace_provider_with_retry", None)
        module.providers["glm"] = fake_provider
        module.call_provider_in_threadpool = fail_if_called
        module.execute_trace_provider_with_retry = fake_execute_trace_provider_with_retry
        try:
            response = asyncio.run(
                module.analyze_trace(
                    "glm",
                    FakeRequest(
                        json.dumps(
                            {
                                "trace_text": "trace body",
                                "prompt": "prompt body",
                                "model": "glm-test",
                                "api_key": "glm-key",
                                "timeout_ms": 30000,
                                "retry_enabled": True,
                                "retry_max_attempts": 4,
                            }
                        ).encode("utf-8")
                    ),
                )
            )
        finally:
            module.providers["glm"] = original_provider
            module.call_provider_in_threadpool = original_call
            if original_execute is None:
                delattr(module, "execute_trace_provider_with_retry")
            else:
                module.execute_trace_provider_with_retry = original_execute

        self.assertTrue(response["ok"])
        self.assertIs(captured["provider"], fake_provider)
        self.assertEqual(captured["kwargs"]["trace_text"], "trace body")
        self.assertEqual(captured["kwargs"]["timeout_ms"], 30000)
        self.assertTrue(captured["kwargs"]["retry_enabled"])
        self.assertEqual(captured["kwargs"]["retry_max_attempts"], 4)
        self.assertEqual(captured["kwargs"]["model"], "glm-test")
        self.assertEqual(captured["kwargs"]["api_key"], "glm-key")

    def test_retry_executor_retries_http_429_with_shared_remaining_budget(self):
        module = load_module("ai_proxy_main_retry_budget", "ai/proxy/main.py")

        class FakeClock:
            def __init__(self):
                self.now = 0.0

            def monotonic(self):
                return self.now

            def sleep(self, seconds: float):
                self.now += seconds

        class FakeProvider:
            def __init__(self, clock):
                self.clock = clock
                self.timeout_history = []
                self.calls = 0

            def analyze_trace(self, **kwargs):
                self.calls += 1
                self.timeout_history.append(kwargs["timeout_ms"])
                if self.calls == 1:
                    # 第一次先消耗 600ms 再返回 429。
                    # 如果重试实现错误地把 timeout 重新置回 3000ms，第二次这里就会暴露出“预算被重置”。
                    self.clock.now += 0.6
                    return {
                        "ok": False,
                        "error_status": "HTTP_ERROR",
                        "http_status": 429,
                        "error_message": "quota exhausted",
                    }
                return {
                    "ok": True,
                    "analysis": {
                        "summary": "ok",
                        "risk_level": "info",
                        "root_cause": "none",
                        "solution": "none",
                    },
                    "usage": None,
                }

        clock = FakeClock()
        provider = FakeProvider(clock)

        result = asyncio.run(
            module.execute_trace_provider_with_retry(
                provider,
                trace_text="trace body",
                prompt="prompt body",
                api_key="glm-key",
                model="glm-test",
                timeout_ms=3000,
                retry_enabled=True,
                retry_max_attempts=3,
                monotonic_fn=clock.monotonic,
                sleep_fn=clock.sleep,
            )
        )

        self.assertTrue(result["ok"])
        self.assertEqual(provider.calls, 2)
        self.assertEqual(provider.timeout_history[0], 3000)
        # 3000ms 总预算里，第一次 provider 已经花了 600ms，再退避 200ms，
        # 所以第二次只允许拿到剩余的 2200ms，而不是重新拿满 3000ms。
        self.assertEqual(provider.timeout_history[1], 2200)

    def test_retry_executor_does_not_retry_http_401(self):
        module = load_module("ai_proxy_main_retry_401", "ai/proxy/main.py")

        class FakeProvider:
            def __init__(self):
                self.calls = 0

            def analyze_trace(self, **kwargs):
                self.calls += 1
                return {
                    "ok": False,
                    "error_status": "HTTP_ERROR",
                    "http_status": 401,
                    "error_message": "bad key",
                }

        provider = FakeProvider()
        result = asyncio.run(
            module.execute_trace_provider_with_retry(
                provider,
                trace_text="trace body",
                prompt="prompt body",
                api_key="glm-key",
                model="glm-test",
                timeout_ms=3000,
                retry_enabled=True,
                retry_max_attempts=3,
            )
        )

        # 401 是确定性的鉴权失败。
        # 如果这里还重试，只是在白白烧预算，不会提高成功率。
        self.assertFalse(result["ok"])
        self.assertEqual(result["error_status"], "HTTP_ERROR")
        self.assertEqual(provider.calls, 1)

    def test_retry_executor_retries_schema_error_only_once(self):
        module = load_module("ai_proxy_main_retry_schema", "ai/proxy/main.py")

        class FakeProvider:
            def __init__(self):
                self.calls = 0

            def analyze_trace(self, **kwargs):
                self.calls += 1
                if self.calls <= 2:
                    return {
                        "ok": False,
                        "error_status": "PROVIDER_SCHEMA_ERROR",
                        "error_message": f"schema drift attempt {self.calls}",
                    }
                return {
                    "ok": True,
                    "analysis": {
                        "summary": "should not reach third attempt",
                        "risk_level": "info",
                        "root_cause": "none",
                        "solution": "none",
                    },
                    "usage": None,
                }

        provider = FakeProvider()
        result = asyncio.run(
            module.execute_trace_provider_with_retry(
                provider,
                trace_text="trace body",
                prompt="prompt body",
                api_key="glm-key",
                model="glm-test",
                timeout_ms=5000,
                retry_enabled=True,
                retry_max_attempts=5,
            )
        )

        # 结构错误允许补一枪，但不能一路补到 max_attempts。
        # 否则稳定的 prompt/schema 设计错误会被伪装成“系统正在努力恢复”。
        self.assertFalse(result["ok"])
        self.assertEqual(result["error_status"], "PROVIDER_SCHEMA_ERROR")
        self.assertEqual(provider.calls, 2)

    def test_glm_provider_uses_inner_timeout_headroom_from_request_timeout_ms(self):
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
                        "prompt_tokens": 1,
                        "completion_tokens": 1,
                        "total_tokens": 2,
                    },
                }

        class FakeClient:
            def __init__(self, *args, **kwargs):
                # 这里锁住“上游 timeout 必须略早于外层 caller timeout”，
                # 否则 provider 和 C++ 都卡同一时刻时，外层会先超时，永远拿不到 proxy 的结构化失败 JSON。
                captured["timeout"] = kwargs.get("timeout")

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
                timeout_ms=30000,
            )

        self.assertTrue(result["ok"])
        self.assertEqual(captured["timeout"], 29.0)


if __name__ == "__main__":
    unittest.main()
