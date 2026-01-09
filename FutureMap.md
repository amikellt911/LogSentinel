# FutureMap

不影响主线交付，时间允许再做。

## 方向（占位）
### Redis（优先级高，收益最直观，hiredis 封装）
- []/results/* 结果短期缓存：trace_id -> analysis_result，TTL 数分钟，减少 SQLite 读压力。
- []/history 分页结果缓存：page/pageSize/level/search 组成缓存 key，TTL 30-120s，降低高频查询成本。
- []告警去重与节流：告警指纹（rule_id + trace_id + time_bucket）做短 TTL，避免重复通知与风暴。
- []AI 结果复用缓存：日志内容 hash/simhash -> 分析结果或摘要，减少重复推理成本。
- []Trace 聚合临时态：短期 Span 状态落 Redis + TTL，支撑乱序/超时补齐（后置）。

### 端侧预筛选（小模型）
### simhash 相似度缓存/结果复用（分级处理与投机采样,L1h和L2）
### MiniMuduo 增加 gRPC/Protobuf 支持
### pybind11 代替 C++ 与 Python 之间的 HTTP 交流
### LangGraph/Agent
### SQLite/B-Tree 迁移到 RocksDB/LSM-Tree
### 线程池任务队列升级为无锁 MPSC Ring Buffer

## 可能增强点（占位）
### 增强点一（待补充）
### 增强点二（待补充）

## 风险与依赖（占位）
### 依赖事项（待补充）
### 风险事项（待补充）
