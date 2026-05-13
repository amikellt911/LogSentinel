# 2026-05-13 接通 Webhook 渠道测试发送（Frontend -> Backend 路由）

## Git Commit Message

feat(settings): 接通 Webhook 探针发送（通过后端 /api/settings/channels/probe 路由）

## Modification

- `server/handlers/ConfigHandler.h`
- `server/handlers/ConfigHandler.cpp`
- `server/src/main.cpp`
- `client/src/views/SettingsPrototype.vue`

## 这次补了哪些注释

- 无新增大量底层核心注释（复用已有格式），在 `ConfigHandler.cpp` 处理 `webhook_url` 时说明了 `Frontend sends webhookUrl` 的参数契约。

## Learning Tips

### Newbie Tips

由于浏览器的同源安全策略（CORS），直接在 Vue 前端发起跨域 POST 请求到外部平台（如飞书）的 API 网关通常会被拦截。最稳健的架构实践是：遇到“需要外发的三方请求”时，优先将其作为指令（Command）传给自己的后端，由后端代为发送。这既避免了跨域问题，又能复用后端成熟的签名计算和外发组件。

### Function Explanation

新增的 `POST /api/settings/channels/probe` 只要求传入单条尚未保存的 `provider/webhookUrl/secret`，后端在内存中临时构建一个 `WebhookNotifier`，并发射一条包含写死演示文本的假 `TraceAlertEvent`。它不需要写表，也不污染既有 `channel` 状态，纯粹作为一个透传验证网关。

### Pitfalls

在 JSON 契约交互时，前端经常用小驼峰（`webhookUrl`），而后端 C++ 的习惯可能更偏向下划线（`webhook_url`）。这里在 `ConfigHandler::handleProbeChannel` 做了防御式取值，既看 `webhookUrl` 也看 `webhook_url`，避免因为两端命名习惯差异导致读取空值。