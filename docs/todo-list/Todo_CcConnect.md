# Todo CcConnect

- [x] 检查本地 cc-connect 安装状态、版本与现有配置
- [x] 参考官方安装文档确认个人微信接入方式与命令
- [x] 备份并改写 `~/.cc-connect/config.toml` 为微信个人号配置
- [x] 执行 `cc-connect weixin setup` 完成登录绑定
- [x] 修复重复 project 导致的无 token 启动失败问题
- [x] 验证前台 `cc-connect` 可正常启动
- [x] 安装并启动 daemon
- [x] 修复 daemon 缺失 `CODEX_API_KEY` 导致 agent 启动失败
- [x] 排查 `/new` 后新会话本地落盘位置与 session 映射
- [x] 定位 `codex resume` 精确 UUID 可用、picker 列表缺失的问题
- [x] 重建 `~/.codex/session_index.jsonl`，补齐缺失的 Codex 会话索引
- [x] 调整 `~/.cc-connect/config.toml` 为 quiet 模式，降低 weixin `ret=-2` 概率
- [x] 重启 daemon 并确认新配置已加载
- [ ] 等待一轮新的微信实测，确认长回复发送是否明显改善
