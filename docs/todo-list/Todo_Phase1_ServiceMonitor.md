# 阶段 1：ServiceMonitor（服务监控）- 新建首页

**开始时间**：2025-12-29
**完成时间**：2025-12-29 ✅
**实际周期**：1 天

---

## 📋 任务列表

### 1.1 页面骨架
- [x] 创建 `client/src/views/ServiceMonitor.vue` 页面
- [x] 设计三段式布局（顶部卡片 + 中部风险/告警 + 底部态势感知流）
- [ ] 添加页面加载动画（分块加载，首屏优先）- 可选优化

### 1.2 业务健康卡片组件
- [x] 创建 `client/src/components/BusinessHealthCards.vue`
- [x] 实现卡片 1：今日业务健康分（98 分，绿色）
  - 圆形进度条或大数字展示
  - 动态颜色（>90 绿，70-90 黄，<70 红）
- [x] 实现卡片 2：当前错误率（2.5%，红色高亮）
  - 百分比展示
  - 趋势箭头（↑ 上升 / ↓ 下降）
- [x] 实现卡片 3：平均业务耗时（450ms）
  - 数字 + 单位
  - 对比上小时变化
- [x] 添加 hover 动画和骨架屏

### 1.3 风险分布组件（复用）
- [x] 从原 Dashboard 复用风险分布饼图
- [x] 创建独立组件 `client/src/components/RiskDistribution.vue`
- [x] ECharts 饼图配置优化（样式统一）
- [ ] 响应式布局（移动端适配）- 待测试

### 1.4 最近告警组件（复用）
- [x] 从原 Dashboard 复用告警列表
- [x] 创建独立组件 `client/src/components/RecentAlerts.vue`
- [x] Element Plus 表格样式优化（深色主题）
- [x] 添加表格操作列（点击查看详情）

### 1.5 AI 态势感知流组件（复用改造）
- [x] **复用 `client/src/views/BatchInsights.vue`**，改造为独立组件 `AISituationStream.vue`
  - 保留现有时间线布局（左侧状态节点 + 右侧卡片）
  - 保留 Mock 数据生成逻辑（作为演示数据）
  - 保留卡片样式（hover 动画、状态颜色、tag 标签）
- [x] **改造内容**：
  - 保持"最新消息在顶部"（维持 `unshift` 模式，符合监控场景心智模型）
  - 添加消息列表上限限制（保留最近 100 条，超过时自动删除最旧消息）
  - 添加"暂停/继续"按钮（控制 `generateBatch` 定时器）
  - 添加加载更多按钮（用户想看历史时，手动点击加载更多）
  - 添加 trace_id 显示（批次编号 → 日志数量 → trace_id）
  - 简化卡片布局（去掉风险数和延迟统计）
  - 列表数量显示（25/100）
- [ ] **组件提取**：
  - 将 Mock 数据生成逻辑提取为可配置的 `useMockStream` Composable - 可选优化
  - 将状态样式函数（`getStatusBorderColor` 等）提取为 utils - 可选优化
- [ ] 延迟加载策略（不阻塞首屏）- 可选优化

### 1.6 国际化
- [x] 添加中文翻译（`client/src/i18n/zh-CN.ts`）
  - `serviceMonitor` 页面标题
  - `healthScore`、`errorRate`、`avgDuration` 等
- [x] 添加英文翻译（`client/src/i18n/en-US.ts`）

---

## ✅ 完成标准

- [x] ServiceMonitor 页面正常显示
- [x] 3 个业务健康卡片正常工作（使用 Mock 数据）
- [x] 风险分布和告警列表正常展示
- [x] AI 态势感知流实时更新，支持暂停/继续
- [x] 中英文翻译完整

---

## 📝 备注

- 所有数据使用 Mock 数据，预留 API 对接点
- 组件遵循深色主题设计
- 响应式布局支持移动端（待测试）

## 🎉 阶段总结

**已完成**：
- ✅ 创建 ServiceMonitor 主页面 + 4 个子组件
- ✅ 实现业务健康卡片（圆形进度条、趋势箭头）
- ✅ 复用风险分布和告警列表组件
- ✅ 改造 AI 态势感知流（暂停/继续、加载更多、trace_id 显示）
- ✅ 简化卡片布局（去掉风险数和延迟）
- ✅ 列表数量统计（25/100）
- ✅ 中英文国际化
- ✅ 路由和导航菜单更新

**优化点（可选）**：
- 页面加载动画（分块加载）
- 移动端响应式测试
- Mock 数据逻辑提取为 Composable
- 延迟加载策略

---

## 2026-03-19：独立原型页探索（不改旧 ServiceMonitor）

- [x] 明确这一步只做“额外新页面”，旧 `client/src/views/ServiceMonitor.vue` 不动
- [x] 新增一个独立 mock 页面，用来验证“服务视角首页”是否和 Trace 查询页拉开分工
- [x] 新页面布局收口为：服务卡片区 + 当前服务聚焦面板 + 服务风险矩阵 + 异常接口排行
- [x] 因“风险矩阵”口径难定义且解释成本高，已替换为“当前服务最近异常 Trace 列表”
- [x] 继续裁掉重复展示：删服务卡 mini trend，删顶部“当前最危险服务”总览卡
- [x] 把新页面挂到左侧栏新增入口，便于直接切换对比旧页面
- [ ] 等用户看完原型后，再决定哪些模块保留、哪些模块删掉或并回旧页面

---

## 2026-04-06：ServiceMonitor 真实运行态接入计划（先接原型页）

### 已对齐的边界
- [x] 本轮先接 `client/src/views/ServiceMonitorPrototype.vue`，旧 `client/src/views/ServiceMonitor.vue` 先不动
- [x] `services_topk` 先取 4，不先追求全量分页
- [x] 服务卡排序先按 `exception_count` 降序，再按 `latest_exception_time_ms` 降序
- [x] 风险等级先收口为 3 档：`Safe(0)`、`Warning(1~5)`、`Error(>=6)`
- [x] `avg_latency_ms` 先按“当前窗口内该服务所有异常 span 的平均耗时”
- [x] `operation_ranking` 先按“窗口内异常 span 次数”统计，不做累计历史榜
- [x] `recent_samples` 先按 `trace_id + service_name` 去重，每个服务只保留最近 3 条
- [x] AI 摘要先直接复用 trace summary，不做按服务 prompt 拆分

### 当前实现顺序

#### A. 先把后端数据契约和骨架接出来
- [x] 定义 `ServiceRuntimeSnapshot`
  - [x] `overview`
  - [x] `services_topk[]`
  - [x] `global_operation_ranking[]`
- [x] 定义 `PrimaryObservation`
  - [x] `trace_id`
  - [x] `trace_end_time_ms`
  - [x] `trace_risk_level`
  - [x] `services[]`
  - [x] `services[].operations[]`
- [x] 定义 `AnalysisObservation`
  - [x] `trace_id`
  - [x] `trace_end_time_ms`
  - [x] `trace_risk_level`
  - [x] `trace_summary`
  - [x] `service_samples[]`
- [x] 定义累加器接口
  - [x] `OnPrimaryCommitted(...)`
  - [x] `OnAnalysisReady(...)`
  - [x] `OnTick(...)`
  - [x] `BuildSnapshot()`
- [x] 把 handler 和路由接上，当前路径为 `/service-monitor/runtime`

#### B. 第一批先做最稳的两块
- [x] 做 `overview`
  - [x] `abnormal_service_count`
  - [x] `abnormal_trace_count`
  - [x] `latest_exception_time_ms`
- [x] 做 `global_operation_ranking[]`
  - [x] `service_name`
  - [x] `operation_name`
  - [x] `count`
  - [x] `avg_latency_ms`

#### C. 再做 `services_topk[]` 的统计部分
- [x] 维护 `service_name -> ServiceState` 的完整 map，不直接在线维护长期 topk 结构
- [x] 发布 snapshot 时再排序截取 top4
- [x] 先做 `service_basic`
  - [x] `service_name`
  - [x] `risk_level`
  - [x] `exception_count`
  - [x] `avg_latency_ms`
  - [x] `latest_exception_time_ms`
- [x] 再做 `operation_ranking[]`
  - [x] 当前服务内按异常 span 次数排序
  - [x] 第一版先取前 6 项供前端展示

#### D. 最后补 `recent_samples[3]`
- [x] `AnalysisObservation` 只负责最近样本和摘要，不再回写统计字段
- [x] 每个服务最多保留最近 3 条样本
- [x] 样本字段先收口为
  - [x] `trace_id`
  - [x] `time_ms`
  - [x] `operation_name`
  - [x] `summary`
  - [x] `duration_ms`
  - [x] `risk_level`
- [x] `operation_name` 先取该服务在该 trace 中的代表异常操作

#### E. 前端联调顺序
- [x] 先把原型页 overview 接真数据
- [x] 再把全局异常操作排行接真数据
- [x] 再把服务卡列表接真数据（当前先接基础统计字段，右侧详情继续保留 mock）
- [x] 最后把当前服务最近异常 Trace 样本接真数据
- [x] 退窗联调前已去掉 `ServiceMonitorPrototype.vue` 的运行态 mock fallback，后端没数据时直接显示空态
- [x] 原型页已增加 10 秒自动刷新，便于观察时间窗进窗/退窗，不必只靠手点刷新

### 暂不做
- [ ] 不做旧 `ServiceMonitor.vue` 真联调
- [ ] 不做服务级 AI prompt 改造
- [ ] 不做分页、全量服务列表、复杂筛选
- [ ] 不做长期在线优先队列 / 红黑树 topk 结构

### 当前边界说明
- [x] 这一刀已经把真实 observation、累加器、handler、路由和主链路接线打通，接口不再是纯空壳
- [x] 当前统计先按“进程存活期累计态 + 周期发布快照”实现，分钟桶时间窗和过期退账下一刀再接
- [x] 前端 `ServiceMonitorPrototype.vue` 已先把顶部 overview 接到 `/service-monitor/runtime`
- [x] 前端 `ServiceMonitorPrototype.vue` 已把右下角全局异常操作排行接到 `/service-monitor/runtime`
- [x] 左侧服务卡基础字段已接到 `/service-monitor/runtime/services_topk`
- [x] 当前服务最近异常 Trace 样本 已接到 `/service-monitor/runtime/services_topk[].recent_samples`
- [x] 右侧当前服务操作表 已接到 `/service-monitor/runtime/services_topk[].operation_ranking`
- [x] 右侧问题摘要与服务摘要 当前先由 `recent_samples[].summary` 派生，不额外新增后端字段
- [x] 已补手工联调脚本 `server/tests/manual_service_monitor_demo.sh`，可直接复用复杂 trace fixture 灌数并拉 runtime JSON
- [x] 已新增 `--no-auto-start-proxy` 开关，方便在受限环境下手动先起 proxy 再联调后端
- [x] 已补联合启动脚本 `server/tests/run_all_and_demo.sh`，可自动编译后端、拉起独立 proxy、执行 demo，并在退出时统一清理后台进程

### F. 2026-04-06：ServiceRuntimeAccumulator 时间窗第一刀
- [x] `ServiceRuntimeAccumulator` 新增单窗口 `30` 分钟配置能力，默认窗口先固定为 30 分钟
- [x] `OnPrimaryCommitted(...)` 不再直接改全局累计态，改为先写“当前活跃分钟桶”
- [x] `OnTick()` 改为只推进“已经封口的分钟”，不把当前活跃分钟提前算进窗口
- [x] `OnTick()` 已接入过期退窗：`minute - window_minutes` 的旧分钟会从窗口累计态里减掉
- [x] `overview / services_topk / global_operation_ranking` 已改为从窗口累计态裁快照，不再吃进程期全量累计
- [x] `recent_samples` 和 `latest_exception_time_ms` 明确保留为“最近态”，不走分钟桶
- [x] 单测已补两条关键口径
  - [x] 同一分钟内 `OnTick()` 不提前发布活跃分钟数据
  - [x] 时间推进超过窗口后，旧分钟统计会退窗消失
- [x] `main.cpp` 已新增 `--service-monitor-window-minutes`，联调时可临时把窗口压到 `1~2` 分钟，不必真等 30 分钟
- [x] `server/tests/run_all_and_demo.sh` 已默认透传 `SERVICE_MONITOR_WINDOW_MINUTES=2`，一条命令即可直接观察退窗
- [x] 联调脚本职责已拆分
  - [x] `manual_service_monitor_demo.sh` 改为只负责起服务和管生命周期
  - [x] `post_random_trace_once.py` 改为只负责发送 1 条随机 trace
  - [x] `run_all_and_demo.sh` 保留为总包装器，负责串起前两者
- [x] 服务监控后端快照发布周期已从 `5s` 压到 `1s`，减少答辩演示时“分钟已封口但榜单还没刷新”的额外等待
- [x] 服务监控桶粒度已从“固定 1 分钟”改成“启动参数控制”，当前默认 `3s`
- [x] `ServiceRuntimeAccumulator` 已按“窗口分钟数 + 桶粒度秒数”自动计算窗口桶数量，不再把窗口长度直接当桶数量
- [x] `main.cpp` 已新增 `--service-monitor-bucket-seconds`
- [x] 联调脚本已默认透传 `SERVICE_MONITOR_BUCKET_SECONDS=3`
- [x] 原型页已拆开“自动轮询请求中”和“手动刷新按钮 loading”，自动刷新不再把按钮长期锁成“刷新中”
- [x] 原型页自动刷新已从 `10s` 压到 `3s`，与后端 `3s` 桶粒度对齐，减少“数据已进窗但页面没拉到”的错觉
- [x] 正式入口 `/service` 已切到 `ServiceMonitorPrototype.vue`，旧 `/service-prototype` 仅保留重定向兼容

### G. 2026-04-07：Dashboard 先收口成系统监控骨架
- [x] 先不接后端真数据，只先把页面语义从“Dashboard 混合页”收口成“系统运行态页”
- [x] 顶部 6 张卡收口为：总处理日志数 / AI 调用总数 / AI 队列等待时间 / AI 推理延迟 / 内存占用 / 背压状态
- [x] 去掉明显 mock 味的副文案，例如 `+12.5% vs last hour`、`⚡ Fast`、`~0.8s avg`
- [x] `TokenMetricsCard` 去掉“节省比例 / 预估成本”，改成总 token / 输入 token / 输出 token / 平均每次 AI 调用 token
- [x] 底部折线图继续只保留两条线：接入速率 / AI 完成速率
- [x] 菜单与页面显示文案先改成“系统监控”，文件名 `Dashboard.vue` 暂时不动
- [x] 背压卡已去掉 `queuePercent` 副文案，只保留综合背压状态，避免和后端多因素判定口径打架

### H. 2026-04-07：Trace AI usage 回传与 token 真值入口
- [x] 先只补 Trace AI 主链，不改数据库 schema，不顺手接 Dashboard 真指标
- [x] Python proxy 的 `/analyze/trace/{provider}` 外层响应补 `usage`
- [x] Gemini provider 把官方 `usage_metadata` 归一化成内部统一字段
- [x] C++ `TraceAiProvider` 返回“analysis + optional usage”，不再只有 `LogAnalysisResult`
- [x] `TraceSessionManager` 告警事件 token_count 优先吃 provider usage，缺失时继续回退本地估算
- [x] 单测补 usage 命中与 usage 缺失两条口径

### I. 2026-04-07：SystemMonitor 后端第一刀（骨架 + 真指标入口）
- [x] 先新增 `SystemRuntimeAccumulator`，定义系统监控快照契约：overview / token_stats / timeseries
- [x] 先写 `SystemRuntimeAccumulator` 失败测试，锁空快照、累计值、差分速率、固定样本平均
- [x] 再实现 `SystemRuntimeAccumulator` 最小逻辑：原子累计、固定样本 ring、定时采样序列
- [x] `SystemRuntimeAccumulator` 已把 `ai_call_total` 和 `ai_completion_total` 拆成 started/completed 两个成熟时机
- [x] AI 延迟样本已改成“一次调用一条复合样本”，不再拆成两套独立窗口
- [ ] 把 `/dashboard` 从旧 `SqliteLogRepository::getDashboardStats()` 切到新快照
- [ ] 先把主链现成真指标接进来：总接收日志数、AI 调用总数、AI 推理延迟、token 总量、背压状态
- [ ] 再补“系统监控缺口埋点”：AI 线程池排队等待时间、接入速率 / AI 完成速率采样
