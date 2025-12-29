# 前端架构改造 TodoList

## 项目概述
将前端从"批次日志分析"改造为"基于 trace_id 的分布式追踪 + 可观测性平台"。

**改造日期**：2025-12-26
**预计周期**：分 4 个阶段完成

> **📌 重要说明**：
> - 本版本**仅关注前端实现**，不考虑后端 API 对接
> - 所有数据展示使用 **Mock 数据**（基于现有 BatchInsights.vue 的 Mock 模式）
> - 在代码中预留 API 对接点（添加 `// TODO: Replace with real API` 注释）
> - 后续阶段再进行前后端联调

---

## 📋 总体进度

- [ ] **阶段 1**：ServiceMonitor（新建首页）
- [ ] **阶段 2**：TraceExplorer（核心追溯功能）
- [ ] **阶段 3**：AIEngine（技术展示）
- [ ] **阶段 4**：SystemStatus（精简）+ 收尾

---

## 🚀 阶段 1：ServiceMonitor（服务监控）- 新建首页

**目标**：创建新的首页，展示业务健康指标 + AI 态势感知

### 1.1 页面骨架
- [ ] 创建 `client/src/views/ServiceMonitor.vue` 页面
- [ ] 设计三段式布局（顶部卡片 + 中部风险/告警 + 底部态势感知流）
- [ ] 添加页面加载动画（分块加载，首屏优先）

### 1.2 业务健康卡片组件
- [ ] 创建 `client/src/components/BusinessHealthCards.vue`
- [ ] 实现卡片 1：今日业务健康分（98 分，绿色）
  - 圆形进度条或大数字展示
  - 动态颜色（>90 绿，70-90 黄，<70 红）
- [ ] 实现卡片 2：当前错误率（2.5%，红色高亮）
  - 百分比展示
  - 趋势箭头（↑ 上升 / ↓ 下降）
- [ ] 实现卡片 3：平均业务耗时（450ms）
  - 数字 + 单位
  - 对比上小时变化
- [ ] 添加 hover 动画和骨架屏

### 1.3 风险分布组件（复用）
- [ ] 从原 Dashboard 复用风险分布饼图
- [ ] 创建独立组件 `client/src/components/RiskDistribution.vue`
- [ ] ECharts 饼图配置优化（样式统一）
- [ ] 响应式布局（移动端适配）

### 1.4 最近告警组件（复用）
- [ ] 从原 Dashboard 复用告警列表
- [ ] 创建独立组件 `client/src/components/RecentAlerts.vue`
- [ ] Element Plus 表格样式优化（深色主题）
- [ ] 添加表格操作列（点击查看详情）

### 1.5 AI 态势感知流组件（复用改造）
- [ ] **复用 `client/src/views/BatchInsights.vue`**，改造为独立组件 `AISituationStream.vue`
  - 保留现有时间线布局（左侧状态节点 + 右侧卡片）
  - 保留 Mock 数据生成逻辑（作为演示数据）
  - 保留卡片样式（hover 动画、状态颜色、tag 标签）
- [ ] **改造内容**：
  - 保持"最新消息在顶部"（维持 `unshift` 模式，符合监控场景心智模型）
  - 添加消息列表上限限制（保留最近 100 条，超过时自动删除最旧消息）
  - 添加"暂停/继续"按钮（控制 `generateBatch` 定时器）
  - 添加加载更多按钮（用户想看历史时，手动点击加载更多）
- [ ] **组件提取**：
  - 将 Mock 数据生成逻辑提取为可配置的 `useMockStream` Composable
  - 将状态样式函数（`getStatusBorderColor` 等）提取为 utils
- [ ] 延迟加载策略（不阻塞首屏）

### 1.7 国际化
- [ ] 添加中文翻译（`client/src/i18n/zh-CN.ts`）
  - `serviceMonitor` 页面标题
  - `healthScore`、`errorRate`、`avgDuration` 等
- [ ] 添加英文翻译（`client/src/i18n/en-US.ts`）

---

## 🔍 阶段 2：TraceExplorer（Trace 追溯）- 核心功能

**目标**：实现 Trace 列表搜索 + 瀑布图展示 + 根因分析

### 2.1 页面骨架
- [ ] 改造 `client/src/views/History.vue` → 重命名为 `TraceExplorer.vue`
- [ ] 设计两段式布局（顶部搜索区 + 底部列表区，占满全屏）
- [ ] 点击列表某一行后，弹出详情抽屉（从右侧滑出，覆盖在列表之上）
- [ ] 抽屉宽度自适应（桌面端占 70%，平板占 80%，移动端占 100%）

### 2.2 搜索表单组件
- [ ] 创建 `client/src/components/TraceSearchBar.vue`
- [ ] 搜索字段：
  - `trace_id`（精确匹配）
  - `service_name`（下拉选择）
  - `time_range`（快捷选择：最近 1h/6h/24h/自定义）
  - `risk_level`（多选：Critical/Error/Warning/Info/Safe）
  - `min_duration`（耗时过滤）
- [ ] 添加重置和搜索按钮
- [ ] URL 参数同步（刷新保持搜索条件）

### 2.3 Trace 列表组件
- [ ] 创建 `client/src/components/TraceListTable.vue`
- [ ] 表格列：
  - trace_id（可点击，打开详情）
  - service_name（主要服务）
  - start_time（开始时间）
  - duration（耗时，ms）
  - span_count（中文：Span 数量，表示 Trace 中的操作节点数）
  - risk_level（风险等级，颜色标识）
  - 操作（查看详情按钮，点击打开详情抽屉）
- [ ] 分页组件（Element Plus Pagination）
- [ ] 排序功能（按时间/耗时/风险）
- [ ] 行点击事件（打开详情抽屉）

### 2.4 瀑布图组件（核心！）
- [ ] 创建 `client/src/components/TraceWaterfall.vue`
- [ ] **技术方案**：ECharts 堆叠条形图（Stacked Bar）
  - X 轴：时间线（相对时间 0ms~500ms）
  - Y 轴：服务名称（按调用顺序排序）
  - 使用透明色占位实现瀑布效果
  - 设置 `barMinWidth: 2` 防止细条看不见
- [ ] **数据结构**：
  ```typescript
  interface TraceSpan {
    span_id: string
    service_name: string
    start_time: number
    duration: number
    parent_id: string | null
    status: 'success' | 'error' | 'warning'
  }
  ```
- [ ] **交互功能**：
  - Hover 显示详细信息（Tooltip）
  - Click 高亮选中 Span
  - 状态颜色映射（成功=绿，错误=红，慢查询=黄）
  - 支持缩放（dataZoom）
- [ ] **备选方案**：如果堆叠条形图效果不好，改用 Custom Series（renderItem）

### 2.5 详情抽屉组件
- [ ] 创建 `client/src/components/TraceDetailDrawer.vue`
- [ ] **布局结构**：
  - **顶部**：AI 根因分析（用户最想看的内容）
    - 风险等级徽章
    - 根因总结
    - 解决建议
  - **中部**：瀑布图（调用链可视化）
  - **底部**：Span 详细列表（可折叠）
- [ ] 抽屉从右侧滑出（宽度 70% 或自适应）
- [ ] 支持全屏切换
- [ ] 关闭时清空数据（释放内存）

### 2.6 TypeScript 接口类型定义（前端内部）
- [ ] 定义前端数据结构类型
  - `TraceListItem`（列表项）
  - `TraceDetail`（详情数据）
  - `TraceSpan`（Span 节点）
  - `AIAnalysis`（AI 分析结果）
- [ ] 使用 Mock 数据进行演示
- [ ] 预留 API 接口对接点（添加 TODO 注释）

### 2.7 国际化
- [ ] 添加中文翻译
  - `traceExplorer` 页面标题
  - 表格列名、搜索标签、操作按钮
- [ ] 添加英文翻译

---

## 🤖 阶段 3：AIEngine（AI 引擎中心）- 技术展示

**目标**：展示 Token 消耗 + 历史批次存档（复用 BatchInsights 样式）+ Prompt 透视

> **💡 概念澄清**：
> - `batch_id` = `trace_id`（在当前系统中是同一个概念）
> - `span_count` = `log_count`（Span 数量就是日志数量）
> - `risk_count` / `error_count` 不单独显示（详情卡片中的 Span 列表已直观展示哪些 Span 有问题）

### 3.1 页面骨架
- [ ] 改造 `client/src/views/BatchInsights.vue` → 重命名为 `AIEngine.vue`
- [ ] 设计**两段式布局**：
  - **顶部**：Token 指标卡片（横向排列，4 个卡片）
  - **底部**：历史批次存档列表（表格形式）

### 3.2 Token 指标卡片组件（简化版）
- [ ] 创建 `client/src/components/TokenMetricsCard.vue`
- [ ] 实现指标展示（横向 4 个卡片）：
  - 今日 Token 消耗总量
  - 节省比例（大数字，85%，绿色突出）
  - 预估成本（$0.02）
  - 平均每批次 Token 数
- [ ] 颜色方案（节省比例用绿色突出）
- [ ] 添加 hover 动画

### 3.3 历史批次存档列表
- [ ] 创建 `client/src/components/BatchArchiveList.vue`
- [ ] **表格列**（只显示关键指标）：
  - trace_id / batch_id（可点击）
  - created_at（创建时间）
  - log_count（日志数量，即 Span 数量）
  - risk_level（风险等级，颜色标识）
  - token_count（Token 消耗）
  - **操作**（两个按钮）：
    - 查看详情按钮 → 点击展开/折叠批次卡片（复用 BatchInsights 的卡片样式）
    - Prompt 透视按钮（开发者图标）→ 点击打开抽屉显示 JSON
- [ ] **详情展开功能**：
  - 点击"查看详情"后，在表格行下方展开一个区域
  - 展示内容**完全复用 BatchInsights.vue 的卡片样式**：
    - 左侧：时间 + 状态节点（带颜色圆圈）
    - 右侧：卡片内容（trace_id、log_count、token_count、avg_latency、risk_level 徽章、summary、tags）
  - 支持折叠
- [ ] 分页组件（Element Plus Pagination）
- [ ] 搜索过滤（按时间范围、风险等级）

### 3.4 Prompt 透视组件（开发者视图）
- [ ] 创建 `client/src/components/PromptDebugger.vue`（抽屉组件，从右侧滑出）
- [ ] **触发方式**：点击表格中的"Prompt 透视"按钮
- [ ] **布局结构**：
  - **顶部**：标题（Prompt 透视 - Batch #1024）+ 关闭按钮
  - **主体**：左右分栏布局
    - **左侧**：Input（发送给 AI 的 Prompt JSON）
    - **右侧**：Output（AI 返回的 Response JSON）
- [ ] **JSON 高亮显示**：
  - 使用 `vue-json-pretty` 库（推荐）
  - 或手动实现 `<pre><code class="language-json">` + Prism.js
  - 语法高亮、折叠/展开
- [ ] **数据展示**：
  - **Input**：
    - `trace_context`：完整的追踪上下文
    - `constraint`：分析约束
    - `system_prompt`：系统提示词
  - **Output**：
    - `risk_level`：风险等级
    - `summary`：总结
    - `root_cause`：根因
    - `solution`：解决方案
  - **元数据**：时间戳、模型名称、耗时、Token 消耗
- [ ] 复制按钮（一键复制 JSON）

### 3.5 TypeScript 接口类型定义（前端内部）
- [ ] 定义前端数据结构类型
  - `TokenMetrics`
  - `BatchArchiveItem`
  - `PromptDebugInfo`
- [ ] 使用 Mock 数据进行演示
- [ ] 预留 API 接口对接点（添加 TODO 注释）

### 3.6 国际化
- [ ] 添加中文翻译
  - `aiEngine` 页面标题
  - Token 相关术语
  - Prompt 透视、开发者视图标签
- [ ] 添加英文翻译

---

## 💻 阶段 4：SystemStatus（系统状态）- 精简

**目标**：精简原 Dashboard，只保留 LogSentinel 自身指标

### 4.1 页面改造
- [ ] 改造 `client/src/views/Dashboard.vue` → 重命名为 `SystemStatus.vue`
- [ ] **删除部分**：
  - 风险分布饼图区域
  - 最近告警列表区域
- [ ] **保留部分**：
  - 6 个性能指标卡片（总日志数、网络延迟、AI 延迟、AI 触发次数、内存、背压）
  - 实时吞吐量图表（日志摄取速率 + AI 处理速率）

### 4.2 组件复用
- [ ] 复用 `MetricCard.vue` 组件（无需修改）
- [ ] 复用 ECharts 图表配置（无需修改）

### 4.3 布局调整
- [ ] 调整为两段式布局（顶部卡片 + 底部图表）
- [ ] 增加留白，提升视觉呼吸感

### 4.4 国际化
- [ ] 修改翻译 key：`dashboard` → `systemStatus`
- [ ] 更新中英文翻译文件

---

## 🔧 阶段 5：路由和导航调整

### 5.1 路由配置
- [ ] 修改 `client/src/router/index.ts`
  - 新增路由：`{ path: '/', name: 'service', component: ServiceMonitor }`（设为首页）
  - 新增路由：`{ path: '/traces', name: 'traces', component: TraceExplorer }`
  - 新增路由：`{ path: '/ai-engine', name: 'ai-engine', component: AIEngine }`
  - 新增路由：`{ path: '/system', name: 'system', component: SystemStatus }`
  - 保留路由：`/logs`（LiveLogs）、`/benchmark`、`/settings`
- [ ] 路由懒加载优化（`() => import(...)`）

### 5.2 导航菜单调整
- [ ] 修改 `client/src/layout/MainLayout.vue`
- [ ] 侧边栏顺序调整：
  1. 📊 Service Monitor（服务监控）- **新增**
  2. 💻 System Status（系统状态）- **新增**
  3. 📋 Live Logs（实时日志）- **原位置**
  4. 🔍 Trace Explorer（Trace 追溯）- **原名为 History**
  5. 🤖 AI Engine（AI 引擎中心）- **原名为 BatchInsights**
  6. ⚡ Benchmark（性能测试）- **原位置**
  7. ⚙️ Settings（设置）- **原位置**
- [ ] 图标优化（使用 Element Plus Icons）
- [ ] 选中态样式优化
- [ ] 响应式适配（移动端收起为汉堡菜单）

### 5.3 页面标题和元数据
- [ ] 更新每个页面的 `<title>`（document.title）
- [ ] 添加页面描述（meta description）

---

## 🌐 阶段 6：国际化全面更新

### 6.1 中文翻译（zh-CN.ts）
- [ ] 添加新页面翻译
  - `serviceMonitor`、`systemStatus`、`traceExplorer`、`aiEngine`
- [ ] 添加新组件翻译
  - 业务健康卡片、Trace 搜索栏、瀑布图、Token 指标等
- [ ] 添加新操作翻译
  - "查看详情"、"开发者视图"、"搜索"、"重置"等
- [ ] 统一术语翻译
  - `trace_id` → Trace ID
  - `span_id` → Span ID
  - `root_cause` → 根因

### 6.2 英文翻译（en-US.ts）
- [ ] 同步所有中文翻译到英文
- [ ] 检查术语一致性（使用业界标准术语）
  - Service Monitor / Trace Explorer / AI Engine / Observability

### 6.3 翻译文件组织
- [ ] 考虑拆分为多个模块（按页面拆分）
- [ ] 或保持单文件（小项目推荐）

---

## 🎨 阶段 7：样式优化和统一

### 7.1 全局样式
- [ ] 统一颜色变量（CSS Variables）
  - 主色、成功、警告、错误、信息
  - 深色主题配色
- [ ] 统一间距规范（4px 基准）
- [ ] 统一圆角、阴影、字体

### 7.2 组件样式
- [ ] 所有新组件遵循相同设计规范
- [ ] 深色模式统一（背景 `#1e1e1e`、卡片 `#2d2d2d`）
- [ ] Element Plus 主题覆盖（如有必要）

### 7.3 响应式适配
- [ ] 所有页面支持移动端（<768px）
- [ ] 卡片自适应（1列 → 2列 → 3列）
- [ ] 抽屉/弹窗自适应宽度
- [ ] 表格横向滚动（小屏幕）

### 7.4 动画和过渡
- [ ] 页面切换动画（淡入淡出）
- [ ] 卡片 hover 动画
- [ ] 抽屉滑出动画
- [ ] 数据加载骨架屏

---

## 📦 阶段 8：依赖安装和配置

### 8.1 新增依赖
- [ ] **JSON 高亮库**（用于 Prompt 透视）
  - 选项 A：`vue-json-pretty`（推荐）
  - 选项 B：`prismjs` + 自定义组件
  - 选项 C：原生 `<pre>` + CSS（轻量）
- [ ] 其他可能的依赖（按需添加）

### 8.2 配置文件
- [ ] 更新 `vite.config.ts`（如有必要）
- [ ] 更新 `tsconfig.json`（路径别名）
- [ ] 更新 `.eslintrc.cjs`（规则调整）

---

## 🧪 阶段 9：测试和优化

### 9.1 功能测试
- [ ] ServiceMonitor 页面
  - [ ] 业务卡片数据加载
  - [ ] 风险分布和告警列表展示
  - [ ] AI 态势感知流实时更新
- [ ] TraceExplorer 页面
  - [ ] 搜索功能（各种条件组合）
  - [ ] Trace 列表排序和分页
  - [ ] 瀑布图渲染和交互
  - [ ] 详情抽屉打开/关闭
- [ ] AIEngine 页面
  - [ ] Token 指标和趋势图
  - [ ] 历史批次列表
  - [ ] Prompt 透视 JSON 高亮
- [ ] SystemStatus 页面
  - [ ] 系统指标展示
  - [ ] 实时图表更新

### 9.2 性能优化
- [ ] 组件懒加载（路由级别）
- [ ] ECharts 图表按需加载（不引入全部）
- [ ] 防抖和节流（搜索、resize）
- [ ] 虚拟滚动（长列表优化）
- [ ] 图片/资源压缩

### 9.3 兼容性测试
- [ ] Chrome/Edge（主要）
- [ ] Firefox
- [ ] Safari（如有条件）
- [ ] 移动端浏览器

### 9.4 错误处理
- [ ] API 请求失败处理
- [ ] 网络超时重试
- [ ] 用户友好错误提示
- [ ] 全局错误捕获（Vue errorHandler）

---

## 📝 阶段 10：文档和收尾

### 10.1 代码注释
- [ ] 所有新组件添加中文注释
- [ ] 复杂逻辑添加"为什么这么写"的解释
- [ ] ECharts 配置添加注释（方便维护）

### 10.2 README 更新
- [ ] 更新前端架构说明
- [ ] 添加新页面截图
- [ ] 更新开发命令

### 10.3 CLAUDE.md 更新
- [ ] 更新项目架构描述
- [ ] 更新模块结构说明
- [ ] 添加新页面功能说明
- [ ] 更新技术栈说明（新增依赖）

### 10.4 Git 提交
- [ ] 每个阶段完成后提交一次
- [ ] 提交信息遵循 Conventional Commits
- [ ] 示例：`feat(client): 新增 ServiceMonitor 首页`

---

## ✅ 验收标准

### 功能完整性
- [x] 所有 4 个新页面正常工作
- [x] 导航菜单顺序正确
- [x] 所有数据展示使用 Mock 数据（预留 API 对接点）
- [x] 国际化支持中英文切换

### 用户体验
- [x] 页面加载速度 < 2s（首屏）
- [x] 交互流畅，无卡顿
- [x] 移动端适配良好
- [x] 错误提示友好

### 代码质量
- [x] TypeScript 类型完整（无 any）
- [x] 组件职责单一，可复用
- [x] 代码注释详细（中文）
- [x] 遵循 Vue 3 Composition API 规范

### 答辩准备
- [x] ServiceMonitor 展示业务监控能力
- [x] TraceExplorer 展示分布式追踪能力（瀑布图）
- [x] AIEngine 展示 Prompt Engineering 和成本控制
- [x] SystemStatus 展示系统性能优化成果

---

## 📅 时间规划（建议）

| 阶段 | 预计时间 | 优先级 |
|------|---------|--------|
| 阶段 1：ServiceMonitor | 2-3 天 | P0（首页） |
| 阶段 2：TraceExplorer | 3-4 天 | P0（核心） |
| 阶段 3：AIEngine | 2-3 天 | P1（技术展示） |
| 阶段 4：SystemStatus | 1 天 | P2（精简） |
| 阶段 5-7：路由/国际化/样式 | 1-2 天 | P0 |
| 阶段 8：依赖配置 | 0.5 天 | P1 |
| 阶段 9：测试优化 | 2-3 天 | P0 |
| 阶段 10：文档收尾 | 1 天 | P1 |
| **总计** | **13-18 天** | - |

---

## 🚀 开始行动

**第一步**：从阶段 1 开始，创建 ServiceMonitor 页面骨架。

**第二步**：实现业务健康卡片组件（3 个核心指标）。

**第三步**：复用风险分布和告警组件，整合到 ServiceMonitor。

**第四步**：实现 AI 态势感知流组件，完成首页。

**后续**：依次完成 TraceExplorer、AIEngine、SystemStatus。

---

祝开发顺利！💪
