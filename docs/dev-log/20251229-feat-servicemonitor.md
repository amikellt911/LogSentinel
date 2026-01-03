# 20251229-feat-servicemonitor.md

## Git Commit Message

```
feat(client): 新增 ServiceMonitor 服务监控首页

- 创建 ServiceMonitor.vue 页面，整合业务健康卡片、风险分布、告警列表、AI 态势感知流
- 创建 BusinessHealthCards.vue 组件，展示今日业务健康分（圆形进度条）、错误率、平均耗时（带趋势箭头）
- 创建 RiskDistribution.vue 组件，复用 ECharts 饼图展示风险分布
- 创建 RecentAlerts.vue 组件，展示最近告警列表（带级别颜色标识）
- 改造 AISituationStream.vue 组件，基于 BatchInsights 添加暂停/继续、加载更多功能
- 在批次卡片中显示 trace_id（格式：trace_001234），明确 trace_id 与 batch_id 的对应关系
- 修复加载更多逻辑：历史批次号递减（minBatchId--），新消息批次号递增（batchCounter++）
- 简化卡片布局：去掉风险数和延迟统计，聚焦 AI 分析总结
- 列表数量显示：25/100（当前数量/最大数量），替代 Token 统计
- 添加国际化翻译（中英文），更新路由和导航菜单
- 所有组件使用 Mock 数据，预留 API 对接点
```

## Modification

### 新增文件
- `client/src/views/ServiceMonitor.vue` - 服务监控主页
- `client/src/components/BusinessHealthCards.vue` - 业务健康卡片组件（3个指标卡片）
- `client/src/components/RiskDistribution.vue` - 风险分布饼图组件
- `client/src/components/RecentAlerts.vue` - 最近告警列表组件
- `client/src/components/AISituationStream.vue` - AI 态势感知流组件

### 修改文件
- `client/src/i18n.ts` - 添加 serviceMonitor 相关的中英文翻译
- `client/src/router/index.ts` - 添加 /service 路由
- `client/src/layout/MainLayout.vue` - 在导航菜单中添加"服务监控"入口

## 设计亮点

### 1. 业务健康卡片
- **今日健康分**：SVG 圆形进度条（stroke-dasharray + stroke-dashoffset），动态颜色（>90绿，70-90黄，<70红）
- **错误率**：红色高亮，带趋势箭头（↑上升 / ↓下降）
- **平均耗时**：蓝色数字，对比上小时变化

### 2. AI 态势感知流卡片优化
- **布局简化**：去掉风险数和延迟统计，聚焦 AI 分析总结
- **trace_id 显示**：批次编号 → 日志数量 → trace_id（蓝色徽章突出）
- **暂停功能**：保留暂停按钮，简化实现（不补回暂停期间数据）
- **列表限制**：显示当前数量/最大数量（25/100），保留最近 100 条

### 3. 加载更多逻辑修复
- **问题**：原实现加载更大批次号（1054），与历史数据逻辑相反
- **修复**：维护两个计数器
  - `batchCounter`：新消息递增（1024 → 1025 → 1026）
  - `minBatchId`：历史消息递减（1024 → 1023 → 1022）

## Learning Tips

### Newbie Tips
1. **SVG 圆形进度条**：`stroke-dasharray` = 圆周长（2πr），`stroke-dashoffset` = 偏移量（实现进度百分比）
2. **Element Plus 表格深色主题**：内联样式覆盖 `--el-table-*` CSS 变量，配合 `:deep()` 伪类
3. **ECharts 响应式**：`onUnmounted` 时调用 `dispose()` 释放内存，避免内存泄漏

### Function Explanation
- `padStart(6, '0')` - ES6 字符串填充（"123" → "000123"）
- `transition-group` - Vue 列表过渡动画组件
- `unshift()` - 数组头部添加（最新消息在顶部）
- `Math.min(...array)` - ES6 展开运算符获取最小值

### Pitfalls
1. **定时器清理**：`onUnmounted` 必须清除定时器，否则内存泄漏
2. **CSS 变量覆盖**：Element Plus 样式需用 `:deep()` 伪类
3. **国际化切换**：图表需监听 `locale` 变化并重新渲染
4. **加载更多逻辑**：区分新消息（批次号递增）和历史消息（批次号递减）

## 下一步计划

- [ ] 阶段 2：TraceExplorer（Trace 追溯）- 核心功能
- [ ] 阶段 3：AIEngine（AI 引擎中心）- 技术展示
- [ ] 阶段 4：SystemStatus（系统状态）- 精简
- [ ] 测试 ServiceMonitor 响应式布局（移动端适配）
