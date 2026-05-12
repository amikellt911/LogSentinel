#!/usr/bin/env python3
import argparse
import json
import urllib.error
import urllib.request
from typing import Any


PROMPT_NAME = "预售尾款结算风险分析"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Import the presale final-payment business prompt through Settings APIs."
    )
    parser.add_argument(
        "--base-url",
        default="http://127.0.0.1:8080",
        help="LogSentinel base URL, default: http://127.0.0.1:8080",
    )
    parser.add_argument(
        "--timeout-sec",
        type=float,
        default=3.0,
        help="HTTP timeout per settings request, default: 3.0",
    )
    return parser.parse_args()


def http_json(base_url: str,
              path: str,
              method: str = "GET",
              payload: Any | None = None,
              timeout_sec: float = 3.0) -> Any:
    body = None
    headers = {"Content-Type": "application/json"}
    if payload is not None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        url=f"{base_url.rstrip('/')}{path}",
        data=body,
        headers=headers,
        method=method,
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout_sec) as response:
            raw = response.read().decode("utf-8", errors="replace")
            if not raw:
                return {}
            return json.loads(raw)
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"{method} {path} failed: HTTP {exc.code}, body={raw}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"{method} {path} failed: {exc}") from exc


def build_prompt_content() -> str:
    # 这里按 SettingsPrototype.vue 的结构化 Prompt JSON 格式生成 content。
    # 不直接写最终 business_guidance 文本，是为了让前端五段表单可以正常回填和编辑。
    # Prompt 只告诉 AI 要检查哪些证据，不预设灰度、滚动更新或配置中心延迟就是根因；
    # 如果模型要提出这些解释，必须从 Trace 字段和调用关系自己推出来。
    content = {
        "domain_goal": (
            "电商交易结算链路异常分析。关注订单、优惠、支付、履约等服务在同一条 Trace 中的"
            "业务状态、优惠试算、金额计算、支付结果和订单状态是否一致。该领域运行在容器化微服务环境中，"
            "同一业务请求可能经过不同服务实例。分析时应关注同一 Trace 内参与同一业务决策的服务实例、"
            "配置快照、优惠项、金额计算和调用关系是否一致；部署、配置同步或实例状态变化只能作为候选解释，"
            "不能脱离字段证据直接下结论。"
        ),
        "business_glossary": [
            {
                "term": "presale_final_payment",
                "meaning": "预售尾款支付场景，用户已支付定金，本次请求用于结算剩余尾款。",
            },
            {
                "term": "campaign_id",
                "meaning": "营销活动 ID，用于标识优惠权益所属活动。相同 campaign_id 通常表示权益来自同一活动规则，需要关注是否存在同源权益重复使用。",
            },
            {
                "term": "deposit_expand",
                "meaning": "定金膨胀权益，表示定金在尾款阶段可放大的抵扣金额。若它与 coupon 来自同一 campaign_id，在预售尾款阶段通常不应重复抵扣。",
            },
            {
                "term": "coupon",
                "meaning": "优惠券或满减券。若与 deposit_expand 属于同一 campaign_id，应按活动互斥规则处理，不能简单叠加。",
            },
            {
                "term": "quote_id",
                "meaning": "优惠服务返回的一次优惠试算结果 ID，用于把优惠试算结果和后续金额计算关联起来。",
            },
            {
                "term": "discount_total",
                "meaning": "优惠服务返回的优惠总额。它需要和 discount_items、final_pay_amount、paid_amount 一起判断，不能单独作为金额正确的证据。",
            },
            {
                "term": "final_pay_amount",
                "meaning": "订单服务计算出的最终尾款支付金额。",
            },
            {
                "term": "paid_amount",
                "meaning": "支付服务实际创建支付单使用的金额。支付成功只代表支付链路成功，不代表结算金额一定正确。",
            },
            {
                "term": "config_snapshot_version",
                "meaning": "服务实例处理当前 Span 时读取到的业务规则配置快照标识。它只能说明该 Span 使用了哪个规则快照；如果同一 Trace 中相关服务快照不同，需要结合 quote_id、discount_items、campaign_id、final_pay_amount、paid_amount 和调用关系判断是否影响业务结果。",
            },
        ],
        "focus_areas": [
            "检查优惠试算结果、订单金额计算结果、支付金额和订单状态之间是否一致。",
            "检查同一 campaign_id 下是否出现 deposit_expand 与 coupon 等同源权益重复抵扣。",
            "检查上游 Span 返回的 quote_id、discount_total 是否被下游金额计算和支付链路一致消费。",
            "当金额、优惠项或支付结果存在异常时，结合相关 Span 的 config_snapshot_version、service_instance 等信息判断是否存在跨服务规则口径不一致；如果要提出部署、配置同步或实例状态变化等原因，必须标明这是基于 Trace 证据的候选解释。",
            "不要只根据 Span status=OK 判断业务安全；技术成功也可能存在结算语义错误。",
        ],
        "risk_preference": [
            "涉及少收款、重复优惠抵扣、错误支付金额、订单状态错误推进的问题，风险等级应从严判断。",
            "如果支付成功但结算金额可能不符合业务规则，不应标记为 safe。",
            "如果同时存在同源权益重复抵扣和支付成功，应优先判断为 critical。",
            "如果只有配置快照版本不一致，但没有金额、优惠项或状态异常证据，不应直接判断为 critical。",
        ],
        "output_preference": [
            "summary 用一句话说明业务影响，不要只描述服务调用成功或失败。",
            "root_cause 必须给出字段证据和调用链证据，不要只写“优惠异常”“金额异常”或“服务异常”。",
            "solution 必须给出可执行动作，例如补充金额一致性校验、核查已支付订单、限制异常优惠组合、改进灰度期间的结算保护。",
            "输出只包含 summary、risk_level、root_cause、solution 四个字段。",
        ],
    }
    return json.dumps(content, ensure_ascii=False)


def normalize_prompt(raw: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": int(raw.get("id") or 0),
        "name": str(raw.get("name") or ""),
        "content": str(raw.get("content") or ""),
        "is_active": 1 if bool(raw.get("is_active")) else 0,
    }


def import_prompt(base_url: str, timeout_sec: float) -> int:
    settings = http_json(base_url, "/api/settings/all", timeout_sec=timeout_sec)
    raw_prompts = settings.get("prompts", [])
    if not isinstance(raw_prompts, list):
        raise RuntimeError("GET /api/settings/all returned invalid prompts shape")

    prompts = [normalize_prompt(item) for item in raw_prompts if isinstance(item, dict)]
    prompt_content = build_prompt_content()

    target_index = next((i for i, item in enumerate(prompts) if item["name"] == PROMPT_NAME), None)
    # 导入脚本不把 prompts 表里的 is_active 当成生效开关。
    # 当前后端冷启动真正按 active_prompt_id 选择 Prompt，所以这里保持 0，避免列表里出现多个 active 标记。
    if target_index is None:
        prompts.append({
            "id": 0,
            "name": PROMPT_NAME,
            "content": prompt_content,
            "is_active": 0,
        })
    else:
        prompts[target_index] = {
            **prompts[target_index],
            "name": PROMPT_NAME,
            "content": prompt_content,
            "is_active": 0,
        }

    # 不直接改 SQLite，而是走 Settings HTTP 接口。
    # 这样导入脚本和前端保存走同一条契约，避免绕过 Repository 的事务、快照和字段兼容逻辑。
    http_json(base_url, "/api/settings/prompts", method="POST", payload=prompts, timeout_sec=timeout_sec)

    refreshed = http_json(base_url, "/api/settings/all", timeout_sec=timeout_sec)
    refreshed_prompts = refreshed.get("prompts", [])
    if not isinstance(refreshed_prompts, list):
        raise RuntimeError("GET /api/settings/all returned invalid prompts shape after import")

    active_prompt_id = 0
    for item in refreshed_prompts:
        if isinstance(item, dict) and item.get("name") == PROMPT_NAME:
            active_prompt_id = int(item.get("id") or 0)
            break
    if active_prompt_id <= 0:
        raise RuntimeError(f"Prompt imported but id was not found: {PROMPT_NAME}")

    # prompts 表和 active_prompt_id 分属两条 Settings 接口，所以这里必须二次提交。
    # 只保存 prompts 而不写 active_prompt_id，后端冷启动时可能仍然选择旧 Prompt。
    config_payload = {
        "items": [
            {
                "key": "active_prompt_id",
                "value": str(active_prompt_id),
            }
        ]
    }
    http_json(base_url, "/api/settings/config", method="POST", payload=config_payload, timeout_sec=timeout_sec)
    return active_prompt_id


def main() -> int:
    args = parse_args()
    active_prompt_id = import_prompt(args.base_url, args.timeout_sec)
    print(f"[presale-prompt] imported prompt: {PROMPT_NAME}")
    print(f"[presale-prompt] active_prompt_id={active_prompt_id}")
    print("[presale-prompt] Restart LogSentinel before running the demo trace.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
