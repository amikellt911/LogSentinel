---
name: windows-remote-chrome
description: 当需要在无头服务器上运行 web-access 技能，但希望借用 Windows 本地 Chrome 的环境（如已登录态、避免服务端安装 Chrome）时，触发此技能，指导用户如何通过 SSH 反向隧道进行 CDP 端口转发。
---

# Windows 本地 Chrome CDP 远程转发指南

当你在服务器（通常是无头的 Linux 环境）上想使用 `web-access` 技能浏览网页，但遇到了以下情况：
1. 服务器上跑 Chrome 太重、配置复杂或存在图形依赖问题。
2. 需要目标网站（如牛客、小红书、微信公众号等）的登录态，但不想在服务器环境中重新扫码或输入密码。
3. 希望能直观地在自己的显示器上看到 AI Agent 是怎么点击、跳转和抓取网页的。

此时，**最佳方案是：将服务器上 `web-access` 发出的 CDP (Chrome DevTools Protocol) 指令，通过 SSH 反向隧道（`-R`）转发到你本地 Windows 电脑的 Chrome 上执行。**

## 🩸 核心痛点与血泪避坑指南（务必牢记）

我们在之前的探索中踩了无数个坑，终于打通了这条跨海大桥。以下是四大致命错误及解决方案：

### 1. PowerShell 路径解析陷阱
- ❌ **错误**：在 PowerShell 直接粘贴带引号和参数的路径 `"C:\Program Files\...\chrome.exe" --remote-debugging-port=9222` 会直接报错（`意外的标记`）。
- ✅ **正确**：必须在命令最前面加上 PowerShell 的调用操作符 `&` 以及一个空格。

### 2. Chrome 启动服务挂起 (starting...)
- ❌ **错误**：如果后台已有 Chrome 进程，新加的 `--remote-debugging-port` 会被静默忽略。且在某些情况下，直接启动会因为沙盒安全机制卡在 `starting...` 状态，根本不监听端口。
- ✅ **正确**：**必须分配一个独立的临时数据目录** `--user-data-dir="C:\chrome-debug-profile"`，强制它作为一个全新的独立实例启动，彻底断开与旧进程和冲突插件的关联。

### 3. SSH 转发目标地址陷阱（最大的坑！）
- ❌ **错误**：常规的转发命令 `ssh -R 9222:127.0.0.1:9222` 或 `ssh -R 9222:localhost:9222`。这会导致服务器端 `curl` 测试时直接报错 `Empty reply from server (Exit Code: 52)`。原因是 Windows 下 Chrome 经常默认绑定 IPv6 的 `::1`，而 SSH 转发找不到 IPv4 的端口。
- ✅ **正确**：**目标地址必须明确指定为 IPv6 的本机地址 `[::1]`**。即：`ssh -R 9222:[::1]:9222`。

### 4. CDP Proxy 兼容性与 WebSocket 路径安全拦截 (UUID 校验)
- ❌ **错误**：较新的 Chrome 版本（如 Beta/Dev 148+）增加了安全校验。远程连接时不能使用默认的固定 WebSocket 路径 `/devtools/browser`，否则会被拒绝并报 `Received network error or non-101 status code`。
- ✅ **正确**：必须在请求 `cdp-proxy` 时，让代理脚本动态去请求 `http://127.0.0.1:9222/json/version`，提取出包含动态 UUID 的真实 `webSocketDebuggerUrl`。
*(注意：如果代理脚本报错 `require is not defined`，说明是在 ESM 环境运行了 CommonJS 语法，需要将 `require('node:http')` 改为使用文件顶部已导入的 `http` 模块。最后，代理进程必须作为常驻后台服务运行，避免被终端杀掉。)*

---

## 🛠️ 具体操作步骤（发给用户执行）

当判定用户需要进行此类操作时，请将以下精简的三步操作发给用户：

### 第一步：在 Windows 本地启动独立的调试版 Chrome

请打开 **PowerShell**，复制并运行以下命令（路径请根据实际情况微调，建议使用 Beta 或 Dev 版本）：

```powershell
& "C:\Program Files\Google\Chrome Beta\Application\chrome.exe" --remote-debugging-port=9222 --user-data-dir="C:\chrome-debug-profile"
```

此时会弹出一个全新的 Chrome 窗口。**请在这个新窗口中，手动登录你需要我操作的目标网站。**

### 第二步：建立 SSH 反向端口转发（关键！）

保持刚才那个 Chrome 窗口打开。在 PowerShell 中新开一个标签页，使用以下命令连接你的服务器：

```powershell
# 明确绑定 IPv6 的本机地址
ssh -R 9222:[::1]:9222 你的用户名@你的服务器IP
```

### 第三步：服务器端验证与代理启动

连接到服务器后，系统需要：
1. 验证联通性：`curl -s http://localhost:9222/json/version`（必须有 JSON 返回）。
2. 启动代理后台：确保 `cdp-proxy.mjs` 修复了 UUID 获取逻辑，并在后台常驻运行。

完成后，即可直接通过 `web-access` 操作 Windows 本地的浏览器了！
