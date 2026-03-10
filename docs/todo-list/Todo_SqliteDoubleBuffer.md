# Todo_SqliteDoubleBuffer

## SQLite 双写缓冲区

- [x] 重新确认 `TraceRepository / SqliteTraceRepository / TraceSessionManager` 当前调用链，锁定双缓冲应插入在 repository 包装层，而不是直接揉进上层线程模型
- [x] 为 `TraceRepository` 增加主数据批量写与分析结果批量写接口，保持旧单条接口先不删除
- [x] 设计并落地 `BufferedTraceRepository` 最小骨架：底层 `sink_`、两条缓冲线、两把锁、条件变量、单 flush 线程、停止标志
- [x] 设计并落地 `PrimaryBufferGroup / AnalysisBufferGroup` 与空桶池、满桶队列的最小数据结构
- [x] 将 `BufferedTraceRepository` 收窄成“持有底层 sink 的缓冲写入器”，只保留 `AppendPrimary / AppendAnalysis` 两个入口
- [ ] 实现前台主数据 append 与分析结果 append 的最小切桶逻辑（`current / next / free / full`）
- [ ] 实现后台单 flush 线程：`wait_for` 定时唤醒、按主数据优先顺序 flush、无锁状态下调用底层 sink
- [ ] 在 `SqliteTraceRepository` 中真正实现 `SavePrimaryBatch / SaveAnalysisBatch`，用“一批一次事务”替代“循环单条事务”
- [ ] 补最小正确性验证：主数据/分析结果都能落库，关闭时不丢最后一批
- [ ] 预留缓冲区状态观测点，后续再考虑和背压联动（第一版先不做联动策略）
- [ ] 追加同日 dev-log，记录为什么双缓冲放在 `TraceRepository` 包装层而不是写死在 SQLite 实现里
