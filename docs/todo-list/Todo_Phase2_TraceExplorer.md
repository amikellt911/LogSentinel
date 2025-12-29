# 阶段 2：TraceExplorer（Trace 追溯）- 核心功能

**目标**：实现 Trace 列表搜索 + 瀑布图展示 + 根因分析

**完成日期**：2025-12-29
**状态**：✅ 已完成

---

## ✅ 完成清单

### 2.1 页面骨架
- [x] 改造 `client/src/views/History.vue` → 重命名为 `TraceExplorer.vue`
- [x] 设计两段式布局（顶部搜索区 + 底部列表区，占满全屏）
- [x] 点击列表某一行后，弹出详情抽屉（从右侧滑出，覆盖在列表之上）
- [x] 抽屉宽度自适应（桌面端占 70%，平板占 80%，移动端占 100%）

### 2.2 搜索表单组件
- [x] 创建 `client/src/components/TraceSearchBar.vue`
- [x] 搜索字段：
  - `trace_id`（精确匹配）
  - `service_name`（下拉选择）
  - `time_range`（快捷选择：最近 1h/6h/24h/自定义）
  - `risk_level`（多选：Critical/Error/Warning/Info/Safe）
  - `min_duration`（耗时过滤）
- [x] 添加重置和搜索按钮

### 2.3 Trace 列表组件
- [x] 创建 `client/src/components/TraceListTable.vue`
- [x] 表格列：
  - trace_id（可点击，打开详情）
  - service_name（主要服务）
  - start_time（开始时间）
  - duration（**用户耗时**，即业务系统从请求开始到结束的真实墙钟时间）
  - span_count（中文：Span 数量，表示 Trace 中的操作节点数）
  - risk_level（风险等级，颜色标识）
  - 操作（查看详情按钮，点击打开详情抽屉）
- [x] 分页组件（Element Plus Pagination）
- [x] 排序功能（按时间/耗时/风险）
- [x] 行点击事件（打开详情抽屉）
- [x] 列宽优化（Trace ID 和 Service Name 自适应，Start Time 和 Duration 缩小）

### 2.4 瀑布图组件（核心！）
- [x] 创建 `client/src/components/TraceWaterfall.vue`
- [x] **技术方案**：ECharts Custom Series + renderItem
  - X 轴：时间线（相对时间 0ms ~ 总耗时）
  - Y 轴：服务泳道（按服务合并，同一服务的所有请求落在同一行）
  - 使用 `renderItem` 手动计算矩形位置，精确控制 x, y, width, height
  - 使用 `filterMode: 'weakFilter'` 确保长 Span 不会被过滤
  - 使用 `ResizeObserver` 解决 Drawer 动画渲染问题
- [x] **交互功能**：
  - X 轴滚轮缩放时间轴（放大/缩小查看细节）
  - X 轴鼠标拖拽平移
  - X 轴底部滑动条（快速跳转）
  - Y 轴滚轮滚动（服务很多时）
  - Hover 显示详细信息（Tooltip，支持国际化）
  - 状态颜色映射（成功=绿，错误=红，警告=黄）
  - 最小宽度 2px，防止极短耗时看不见
- [x] **数据结构**：
  ```typescript
  interface TraceSpan {
    span_id: string
    service_name: string
    start_time: number
    duration: number
    parent_id: string | null
    status: 'success' | 'error' | 'warning'
    operation?: string
  }
  ```

### 2.5 详情抽屉组件
- [x] 创建 `client/src/components/TraceDetailDrawer.vue`
- [x] **布局结构**：
  - **顶部**：AI 根因分析（用户最想看的内容）
    - 风险等级徽章
    - 根因总结
    - 解决建议
  - **中部**：瀑布图（调用链可视化）
  - **底部**：Span 详细列表（可折叠）
- [x] 抽屉从右侧滑出（宽度 70% 或自适应）
- [x] 移除全屏切换按钮（Element Plus 无此图标）
- [x] 关闭时清空数据（释放内存）
- [x] 只保留一个关闭按钮（移除重复的手动按钮）

### 2.6 TypeScript 接口类型定义（前端内部）
- [x] 定义前端数据结构类型
  - `TraceListItem`（列表项）
  - `TraceDetail`（详情数据）
  - `TraceSpan`（Span 节点）
  - `AIAnalysis`（AI 分析结果）
- [x] 使用 Mock 数据进行演示
- [x] 预留 API 接口对接点（添加 TODO 注释）

### 2.7 国际化
- [x] 添加中文翻译
  - `traceExplorer` 页面标题
  - 表格列名、搜索标签、操作按钮
  - 瀑布图 tooltip 字段（Span ID、操作、开始时间、耗时、状态）
- [x] 添加英文翻译
- [x] 使用 `useI18n()` 实现动态切换

---

## 🎯 核心技术亮点

### 1. 瀑布图实现方案演进

**尝试 1：散点图（Scatter）**
- 使用 `type: 'scatter'` + `symbol: 'rect'`
- 问题：散点图符号是**中心对齐**，导致时间轴偏差
- 例如：start_time=100ms, duration=50ms，矩形中心在 100ms，实际显示 75-125ms

**尝试 2：堆叠条形图（Stacked Bar）**
- 问题：所有条形堆叠在一起，无法实现瀑布效果

**✅ 最终方案：Custom Series + renderItem**
```typescript
{
  type: 'custom',
  renderItem: (params, api) => {
    const startPoint = api.coord([start, categoryIndex])
    const endPoint = api.coord([start + duration, categoryIndex])
    return {
      type: 'rect',
      shape: {
        x: startPoint[0],              // 精确的左边界
        width: endPoint[0] - startPoint[0],  // 精确的宽度
        height: height,
        y: startPoint[1] - height / 2
      }
    }
  }
}
```

### 2. 服务泳道（按服务合并）

**问题**：每个 Span 独占一行，Y 轴过长（几十行）

**解决**：将 Y 轴改为唯一服务名列表，同一服务的所有请求落在同一行
```typescript
const uniqueServices = Array.from(new Set(list.map(s => s.service_name)))
const serviceIndexMap = {}
uniqueServices.forEach((name, index) => {
  serviceIndexMap[name] = index
})
```

### 3. ResizeObserver 解决 Drawer 渲染问题

**问题**：el-drawer 有动画过程，初始 `display: none`，`window.resize` 无法监听

**解决**：使用 `ResizeObserver` 监听容器元素尺寸变化
```typescript
resizeObserver = new ResizeObserver(() => {
  chartInstance.value?.resize()
})
resizeObserver.observe(chartRef.value)
```

### 4. X 轴完整交互支持

**配置**：
- `filterMode: 'weakFilter'`：确保长 Span 不会被错误过滤
- `zoomOnMouseWheel: true`：滚轮缩放时间
- `moveOnMouseMove: true`：鼠标拖拽平移
- `type: 'slider'`：底部滑动条

### 5. Span 状态三级分类

| 状态 | 颜色 | 含义 | 示例场景 |
|------|------|------|---------|
| **Success** | 🟢 绿色 | 一切正常 | HTTP 200, SQL 查询快速 |
| **Warning** | 🟡 黄色 | 成功但有隐患 | 慢查询（>2s）、重试成功、降级服务 |
| **Error** | 🔴 红色 | 请求失败 | HTTP 500, 超时、异常 |

**为什么需要 Warning？**
在微服务架构中，"慢"往往比"错"更致命：
- 错误 = 明确的失败，容易定位
- 慢 = 隐形的问题，影响用户体验但难排查

---

## 📝 重要概念说明

### Trace Duration（用户耗时 vs 分析耗时）

**列表显示的 Duration = 用户耗时（业务耗时）**

定义：用户服务从请求开始到结束的真实墙钟时间
```javascript
duration = max(spans.map(s => s.start_time + s.duration)) - min(spans.map(s => s.start_time))
```

**为什么不包含分析耗时？**
- 用户关心的是"我的服务有多慢"
- 不是"LogSentinel 分析用了多久"
- 这是用户的系统监控，不是 LogSentinel 的性能监控

**如何知道 Trace "结束了"？**
- ✅ 动态计算：每次收到新 Span 时，重新计算 `max(end_time) - min(start_time)`
- ❌ 不等待超时：不包含 LogSentinel 的等待时间

---

## 🐛 遇到的问题和解决

### 问题 1：瀑布图条形起点都对齐在 0
**原因**：错误使用了 scatter 散点图，符号是中心对齐
**解决**：改用 Custom Series + renderItem 手动计算矩形坐标

### 问题 2：散点图不够直观
**原因**：用户反馈散点图没有条形图直观
**解决**：使用 `symbol: 'rect'`，但最终因为坐标对齐问题改用 Custom Series

### 问题 3：Drawer 中瀑布图宽度为 0
**原因**：el-drawer 有动画，初始 `display: none`，window.resize 无法触发
**解决**：使用 ResizeObserver 监听容器尺寸变化

### 问题 4：Y 轴太长（几十个 Span）
**原因**：每个 Span 独占一行
**解决**：改为服务泳道（按服务合并），Y 轴只显示唯一服务名（5-6 个）

### 问题 5：长 Span 被 dataZoom 过滤掉
**原因**：默认 filterMode 会在 Span 中心不在视图内时隐藏它
**解决**：使用 `filterMode: 'weakFilter'`，只要 Span 有任何部分在视图内就显示

### 问题 6：两个关闭按钮
**原因**：Element Plus Drawer 自带关闭按钮，又手动添加了一个
**解决**：移除手动添加的关闭按钮和 Close 图标导入

### 问题 7：瀑布图"固定不动"
**原因**：没有配置 X 轴的 dataZoom
**解决**：添加 X 轴的 slider 和 inside dataZoom，支持滚轮缩放和拖拽平移

---

## 📚 参考文档

- [ECharts Custom Series](https://echarts.apache.org/zh/option.html#series-custom)
- [ResizeObserver API](https://developer.mozilla.org/zh-CN/docs/Web/API/ResizeObserver)
- [分布式追踪最佳实践](https://opentelemetry.io/docs/concepts/observability-primer/)

---

## ✅ 验收标准

- [x] Trace 列表正常展示 Mock 数据
- [x] 搜索功能正常工作（各条件组合）
- [x] 点击行/按钮打开详情抽屉
- [x] 瀑布图正确渲染（时间轴对齐准确）
- [x] 瀑布图支持缩放、平移、滚动
- [x] Tooltip 显示完整信息（含 Span ID）
- [x] 国际化支持中英文切换
- [x] 移动端响应式布局正常
- [x] 无控制台错误

---

**下一步**：阶段 3 - AIEngine（AI 引擎中心）
