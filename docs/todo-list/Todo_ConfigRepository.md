# Todo_ConfigRepository

- [x] 修复 `handleUpdateAppConfig` 中 `sqlite3_step` 错误码误用问题
- [x] 修复 `handleUpdatePrompt` 事务边界（prepare 失败时保证 rollback）
- [x] 为配置写路径补齐 `COMMIT` 返回值检查
- [x] 编译并执行最小回归验证
- [x] 追加同日 dev-log 记录
