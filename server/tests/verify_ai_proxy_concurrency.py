#!/usr/bin/env python3
"""
验证 AI Proxy 是否已经把同步 provider 调用从 event loop 中卸载出去。

既然 mock provider 内部固定会 sleep 约 0.5s，那么如果 Python proxy 还是单车道串行，
N 个并发请求的总耗时就会接近 N * 0.5s。接着如果路由层已经把阻塞 provider 丢进线程池，
那么这些请求的总墙钟时间就不该再线性叠加。
"""

import argparse
import concurrent.futures
import json
import threading
import time
import urllib.error
import urllib.request


def parse_args():
    parser = argparse.ArgumentParser(description="验证 AI Proxy 的 mock 并发能力")
    parser.add_argument(
        "--url",
        default="http://127.0.0.1:8001/analyze/trace/mock",
        help="目标 AI Proxy 接口，默认直连 mock trace 分析端点",
    )
    parser.add_argument(
        "--concurrency",
        type=int,
        default=5,
        help="同时发出的请求数。这个脚本只做一轮固定并发，不做持续压测",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="单个 HTTP 请求超时时间（秒）",
    )
    parser.add_argument(
        "--payload",
        default="error span: mock concurrency validation",
        help="POST 的纯文本 body",
    )
    parser.add_argument(
        "--mock-min-delay-sec",
        type=float,
        default=0.5,
        help="当前 MockProvider 的最小 sleep 时间，用来估算串行下界",
    )
    parser.add_argument(
        "--serial-ratio-threshold",
        type=float,
        default=0.7,
        help=(
            "如果总耗时达到 串行理论下界 * 该比例，就认为仍然太像串行。"
            "默认 0.7，目的不是卡极限，而是排除明显单车道。"
        ),
    )
    return parser.parse_args()


def send_one(url, payload, timeout, barrier, index):
    """
    单个工作线程发送一次请求。

    既然我们想看固定并发场景下的总墙钟时间，那么所有线程要先在 barrier 对齐，
    接着再几乎同时出发，不然就会被“线程启动时差”污染结果。
    """
    barrier.wait()
    started = time.perf_counter()
    request = urllib.request.Request(
        url=url,
        data=payload.encode("utf-8"),
        headers={"Content-Type": "text/plain; charset=utf-8"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8")
            elapsed = time.perf_counter() - started
            return {
                "index": index,
                "ok": True,
                "status": response.status,
                "elapsed": elapsed,
                "body": body,
            }
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        elapsed = time.perf_counter() - started
        return {
            "index": index,
            "ok": False,
            "status": exc.code,
            "elapsed": elapsed,
            "body": body,
        }
    except Exception as exc:  # noqa: BLE001
        elapsed = time.perf_counter() - started
        return {
            "index": index,
            "ok": False,
            "status": None,
            "elapsed": elapsed,
            "body": str(exc),
        }


def validate_results(results, args, total_elapsed):
    """
    按“先接口正确，再并发是否脱离串行”这条时间线做校验。
    """
    failed = [item for item in results if not item["ok"]]
    if failed:
        print("validation=failed")
        print("reason=存在失败请求")
        for item in failed:
            print(
                f"failed_request index={item['index']} status={item['status']} "
                f"elapsed={item['elapsed']:.3f}s body={item['body']}"
            )
        return 1

    for item in results:
        parsed = json.loads(item["body"])
        if parsed.get("provider") != "mock":
            print("validation=failed")
            print(f"reason=返回 provider 不是 mock，index={item['index']} body={item['body']}")
            return 1
        if "analysis" not in parsed:
            print("validation=failed")
            print(f"reason=返回里没有 analysis 字段，index={item['index']} body={item['body']}")
            return 1

    serial_floor = args.concurrency * args.mock_min_delay_sec
    suspicious_threshold = serial_floor * args.serial_ratio_threshold

    print(f"total_elapsed={total_elapsed:.3f}s")
    print(f"serial_floor={serial_floor:.3f}s")
    print(f"suspicious_threshold={suspicious_threshold:.3f}s")

    if total_elapsed >= suspicious_threshold:
        print("validation=failed")
        print(
            "reason=总耗时仍然太接近串行理论下界，当前 proxy 仍然像单车道阻塞"
        )
        return 2

    print("validation=passed")
    print("reason=总耗时明显低于串行理论下界，说明请求已经能并发穿过 mock sleep")
    return 0


def main():
    args = parse_args()
    barrier = threading.Barrier(args.concurrency)

    print(f"url={args.url}")
    print(f"concurrency={args.concurrency}")
    print(f"timeout={args.timeout}")
    print(f"payload={args.payload}")

    started = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        futures = [
            executor.submit(
                send_one,
                args.url,
                args.payload,
                args.timeout,
                barrier,
                index,
            )
            for index in range(args.concurrency)
        ]
        results = [future.result() for future in futures]
    total_elapsed = time.perf_counter() - started

    for item in sorted(results, key=lambda value: value["index"]):
        print(
            f"request index={item['index']} status={item['status']} "
            f"elapsed={item['elapsed']:.3f}s ok={item['ok']}"
        )

    raise SystemExit(validate_results(results, args, total_elapsed))


if __name__ == "__main__":
    main()
