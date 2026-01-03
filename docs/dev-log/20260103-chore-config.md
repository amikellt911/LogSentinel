## Git Commit Message
- chore(config): 提示词合并与微服务文档更新
- chore(config): 移除提示词迁移并修正测试

## Modification
- server/persistence/SqliteConfigRepository.cpp：删除旧版 map/reduce 迁移路径，保留单表初始化与快照逻辑。
- server/tests/SqliteConfigRepository_test.cpp：更新提示词相关单测，适配单列表与无类型偏移的模型。

## Learning Tips
- **Newbie Tips**：重构配置表时要确保测试数据与生产表结构一致，避免沿用旧字段导致解析崩溃；同时保持初始化 SQL 与序列化字段严格一一对应。
- **Function Explanation**：`SqliteConfigRepository::handleUpdatePrompt` 通过事务批量 upsert，并在提交后生成新的快照；依赖 `StmtPtr` RAII 自动释放 SQLite 语句，避免资源泄漏。
- **Pitfalls**：删除迁移逻辑后，如果本地残留旧库文件会导致字段缺失错误，务必先清理数据库或重新初始化；单元测试使用独立 db 文件时需清理 `-wal`/`-shm`，否则会残留状态影响断言。
