# 阶段 1：ServiceMonitor（服务监控）- 新建首页

**开始时间**：2025-12-29
**预计周期**：2-3 天

---

## 📋 任务列表

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

### 1.6 国际化
- [ ] 添加中文翻译（`client/src/i18n/zh-CN.ts`）
  - `serviceMonitor` 页面标题
  - `healthScore`、`errorRate`、`avgDuration` 等
- [ ] 添加英文翻译（`client/src/i18n/en-US.ts`）

---

## ✅ 完成标准

- [ ] ServiceMonitor 页面正常显示
- [ ] 3 个业务健康卡片正常工作（使用 Mock 数据）
- [ ] 风险分布和告警列表正常展示
- [ ] AI 态势感知流实时更新，支持暂停/继续
- [ ] 中英文翻译完整

---

## 📝 备注

- 所有数据使用 Mock 数据，预留 API 对接点
- 组件遵循深色主题设计
- 响应式布局支持移动端
