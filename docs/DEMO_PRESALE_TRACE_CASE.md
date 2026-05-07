# 预售尾款结算 Trace 演示案例

本文档记录 v1.0.0 答辩演示主案例的已确定内容，后续脚本、前端演示和答辩讲解都以这里为准。

本案例的目标不是证明模型“看到了错误日志”，而是证明 LogSentinel 可以把同一条 Trace 下的跨服务 Span、业务属性、业务 Prompt 和 AI 结构化输出串起来，识别技术状态成功但业务语义错误的隐性风险。

## 1. 案例边界

场景名称：

预售尾款结算场景下，灰度/滚动更新期间跨服务规则快照不一致导致同源权益重复抵扣

核心故事：

一次预售尾款结算请求进入系统。`order-service` 所在实例已经使用新的业务规则快照 `promo_rule_v18`，但它调用 `promotion-service` 时命中了仍在旧规则快照 `promo_rule_v17` 上的实例。旧实例返回了同一活动 `PRESALE_0428` 下的 `deposit_expand` 和 `coupon` 两类优惠。`order-service` 消费该优惠试算结果并计算尾款为 700 元，`payment-service` 随后成功创建支付单，订单状态也推进为已支付。

整条链路所有 Span 的技术状态都是 `OK`，但从业务语义看，同一活动下的定金膨胀权益和优惠券被重复抵扣，平台存在少收款风险。

现实解释：

该问题可以发生在 Docker/K8s 等容器化微服务环境中。灰度发布、滚动更新或配置中心推送延迟会导致不同服务实例在短时间内使用不同的配置快照。本系统当前不读取 K8s 事件、发布流水线或配置中心日志，只基于同一 Trace 内的 Span 拓扑、Span attributes 和业务 Prompt 给出候选根因。

字段约束：

Span attributes 只能记录业务事实，不能写入结论。禁止使用 `demo_case`、`case_name`、`gray_rule_mismatch`、`version_mismatch`、`same_source_violation`、`expected_amount`、`root_cause`、`is_wrong` 等会把答案直接喂给 AI 的字段。

规则版本字段统一使用中性的 `config_snapshot_version`，不要使用 `promotion_rule_version` 这类过度定制的字段。

## 2. 完整 Span 树

Trace 标识：

`trace_key=9001001`

调用树：

```text
api-gateway: POST /checkout/final-payment
└── order-service: settle_presale_final_payment
    ├── order-service: load_order_and_settlement_context
    ├── promotion-service: quote_presale_discount
    ├── order-service: calculate_final_pay_amount
    ├── payment-service: create_final_payment_order
    └── order-service: mark_order_paid
```

### 2.1 api-gateway: POST /checkout/final-payment

基础字段：

```text
span_id=1
parent_span_id=0
service=api-gateway
operation=POST /checkout/final-payment
kind=SERVER
status=OK
start=0ms
end=260ms
trace_end=true
```

职责：

作为请求入口，接收移动端发起的预售尾款支付请求，并把请求转发到订单结算链路。

attributes：

```json
{
  "route": "/checkout/final-payment",
  "request_channel": "mobile_app"
}
```

### 2.2 order-service: settle_presale_final_payment

基础字段：

```text
span_id=2
parent_span_id=1
service=order-service
operation=settle_presale_final_payment
kind=SERVER
status=OK
start=10ms
end=245ms
trace_end=false
```

职责：

作为预售尾款结算主编排 Span，串联订单上下文加载、优惠试算、金额计算、支付单创建和订单状态推进。

attributes：

```json
{
  "order_id": "ORD202605070001",
  "campaign_id": "PRESALE_0428",
  "settlement_scene": "presale_final_payment"
}
```

### 2.3 order-service: load_order_and_settlement_context

基础字段：

```text
span_id=3
parent_span_id=2
service=order-service
operation=load_order_and_settlement_context
kind=INTERNAL
status=OK
start=20ms
end=45ms
trace_end=false
```

职责：

加载订单、定金、活动和用户权益上下文，为后续优惠试算和尾款计算提供输入事实。

attributes：

```json
{
  "order_id": "ORD202605070001",
  "deposit_paid_amount": "100",
  "original_final_amount": "1000",
  "campaign_id": "PRESALE_0428"
}
```

### 2.4 promotion-service: quote_presale_discount

基础字段：

```text
span_id=4
parent_span_id=2
service=promotion-service
operation=quote_presale_discount
kind=CLIENT
status=OK
start=55ms
end=105ms
trace_end=false
```

职责：

对预售尾款阶段可用优惠进行试算。本案例中，该调用命中了仍使用 `promo_rule_v17` 的旧 promotion-service 实例。

attributes：

```json
{
  "config_snapshot_version": "promo_rule_v17",
  "service_instance": "promotion-6d4b7f5c8b-r8m2q",
  "quote_id": "QUOTE8821",
  "campaign_id": "PRESALE_0428",
  "discount_items": "[{\"type\":\"deposit_expand\",\"amount\":200,\"campaign_id\":\"PRESALE_0428\"},{\"type\":\"coupon\",\"amount\":100,\"campaign_id\":\"PRESALE_0428\"}]",
  "discount_total": "300"
}
```

### 2.5 order-service: calculate_final_pay_amount

基础字段：

```text
span_id=5
parent_span_id=2
service=order-service
operation=calculate_final_pay_amount
kind=INTERNAL
status=OK
start=115ms
end=140ms
trace_end=false
```

职责：

消费 `promotion-service` 返回的优惠试算结果，计算本次预售尾款实际支付金额。

attributes：

```json
{
  "config_snapshot_version": "promo_rule_v18",
  "service_instance": "order-7c9f6b8d5f-k2p9x",
  "quote_id": "QUOTE8821",
  "original_final_amount": "1000",
  "applied_discount_total": "300",
  "final_pay_amount": "700",
  "currency": "CNY"
}
```

### 2.6 payment-service: create_final_payment_order

基础字段：

```text
span_id=6
parent_span_id=2
service=payment-service
operation=create_final_payment_order
kind=CLIENT
status=OK
start=150ms
end=210ms
trace_end=false
```

职责：

根据订单服务计算出的尾款金额创建支付单。支付成功只说明支付链路完成，不说明结算金额一定正确。

attributes：

```json
{
  "payment_order_id": "PAY202605070001",
  "paid_amount": "700",
  "currency": "CNY",
  "payment_status": "success"
}
```

### 2.7 order-service: mark_order_paid

基础字段：

```text
span_id=7
parent_span_id=2
service=order-service
operation=mark_order_paid
kind=INTERNAL
status=OK
start=220ms
end=240ms
trace_end=false
```

职责：

在支付单创建成功后推进订单状态。本案例用于体现技术状态成功之后，业务风险仍可能已经落地。

attributes：

```json
{
  "order_id": "ORD202605070001",
  "order_status": "PAID",
  "paid_amount": "700"
}
```

## 3. 业务 Prompt

Prompt 名称：

预售尾款结算风险分析

### 3.1 目标领域

```text
电商交易结算链路异常分析。关注订单、优惠、支付、履约等服务在同一条 Trace 中的业务状态、优惠试算、金额计算、支付结果和订单状态是否一致。该领域运行在 Docker/K8s 等容器化微服务环境中，灰度发布、滚动更新或配置中心推送延迟可能让不同服务实例短时间内使用不同业务规则快照，但版本差异必须结合金额、优惠项和调用关系判断，不能单独作为异常结论。
```

### 3.2 业务术语表

```text
presale_final_payment：
预售尾款支付场景，用户已支付定金，本次请求用于结算剩余尾款。

campaign_id：
营销活动 ID，用于标识优惠权益所属活动。相同 campaign_id 通常表示权益来自同一活动规则，需要关注是否存在同源权益重复使用。

deposit_expand：
定金膨胀权益，表示定金在尾款阶段可放大的抵扣金额。若它与 coupon 来自同一 campaign_id，在预售尾款阶段通常不应重复抵扣。

coupon：
优惠券或满减券。若与 deposit_expand 属于同一 campaign_id，应按活动互斥规则处理，不能简单叠加。

quote_id：
优惠服务返回的一次优惠试算结果 ID，用于把优惠试算结果和后续金额计算关联起来。

discount_total：
优惠服务返回的优惠总额。它需要和 discount_items、final_pay_amount、paid_amount 一起判断，不能单独作为金额正确的证据。

final_pay_amount：
订单服务计算出的最终尾款支付金额。

paid_amount：
支付服务实际创建支付单使用的金额。支付成功只代表支付链路成功，不代表结算金额一定正确。

config_snapshot_version：
服务实例处理当前 Span 时使用的业务规则配置快照标识。在 Docker/K8s 等容器化微服务环境中，灰度发布、滚动更新或配置中心推送延迟可能导致不同服务实例短时间内使用不同快照。版本差异只是排障证据，必须结合金额、优惠项和调用关系判断是否构成真实风险。
```

### 3.3 关注点

```text
检查优惠试算结果、订单金额计算结果、支付金额和订单状态之间是否一致。

检查同一 campaign_id 下是否出现 deposit_expand 与 coupon 等同源权益重复抵扣。

检查上游 Span 返回的 quote_id、discount_total 是否被下游金额计算和支付链路一致消费。

当金额、优惠项或支付结果存在异常时，结合相关 Span 的 config_snapshot_version、service_instance 等信息判断是否存在灰度发布、滚动更新或配置传播延迟导致的跨服务规则口径不一致。

不要只根据 Span status=OK 判断业务安全；技术成功也可能存在结算语义错误。
```

### 3.4 风险偏好

```text
涉及少收款、重复优惠抵扣、错误支付金额、订单状态错误推进的问题，风险等级应从严判断。

如果支付成功但结算金额可能不符合业务规则，不应标记为 safe。

如果同时存在同源权益重复抵扣和支付成功，应优先判断为 critical。

如果只有配置快照版本不一致，但没有金额、优惠项或状态异常证据，不应直接判断为 critical。
```

### 3.5 输出偏好

```text
summary 用一句话说明业务影响，不要只描述服务调用成功或失败。

root_cause 必须给出字段证据和调用链证据，不要只写“优惠异常”“金额异常”或“服务异常”。

solution 必须给出可执行动作，例如补充金额一致性校验、核查已支付订单、限制异常优惠组合、改进灰度期间的结算保护。

输出只包含 summary、risk_level、root_cause、solution 四个字段。
```

## 4. 期望的标准 AI 输出

AI 最终输出契约固定为 4 个字段：`summary`、`risk_level`、`root_cause`、`solution`。

参考标准答案：

```json
{
  "summary": "预售尾款结算链路技术状态成功，但同一活动下定金膨胀和优惠券被重复抵扣，存在少收款风险。",
  "risk_level": "critical",
  "root_cause": "promotion-service 在优惠试算 Span 中使用 config_snapshot_version=promo_rule_v17 返回 quote_id=QUOTE8821，discount_items 同时包含 campaign_id=PRESALE_0428 下的 deposit_expand 和 coupon，总优惠 discount_total=300；order-service 在金额计算 Span 中使用 config_snapshot_version=promo_rule_v18 消费同一个 quote_id，并计算 final_pay_amount=700。结合预售尾款同源权益互斥规则，旧优惠试算结果被新结算链路消费，跨服务规则快照不一致导致同源权益重复抵扣。",
  "solution": "在预售尾款结算链路中冻结同一请求的业务规则快照，或在 order-service 消费 promotion quote 时校验 config_snapshot_version、campaign_id 和 discount_items；对已支付且同时包含 deposit_expand 与 coupon 的订单进行补偿核查，并在灰度/滚动更新期间增加跨服务规则版本一致性保护。"
}
```

判分口径：

如果 AI 只说“优惠计算异常”，说明它没有利用 Trace 拓扑、业务 Prompt 或版本属性完成根因定位。

如果 AI 只说“配置版本不一致”，但没有指出同一 `campaign_id` 下的 `deposit_expand` 与 `coupon` 重复抵扣，也是不完整结论。

如果 AI 同时指出技术状态成功、支付金额已落地、同源权益重复抵扣、`promotion-service` 与 `order-service` 的 `config_snapshot_version` 不一致，并给出补偿核查与灰度保护建议，则认为本案例达到演示目标。
