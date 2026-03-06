# Todo_ConfigRepository

- [x] 修复 `handleUpdateAppConfig` 中 `sqlite3_step` 错误码误用问题
- [x] 修复 `handleUpdatePrompt` 事务边界（prepare 失败时保证 rollback）
- [x] 为配置写路径补齐 `COMMIT` 返回值检查
- [x] 编译并执行最小回归验证
- [x] 追加同日 dev-log 记录
- [x] 统一 `ai_provider` / `ai_language` 的后端存储值与前端显示值
- [x] 为历史值补充归一化映射，兼容旧数据读取与保存
- [x] 回归验证后端构建与相关最小链路
- [x] 统一 `channels.provider` / `channels.alert_threshold` 的前后端取值语义
- [x] 将 `ai_fallback_model` 默认值收口到 canonical 值
