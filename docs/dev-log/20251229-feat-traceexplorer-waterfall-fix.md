# 20251229-feat-traceexplorer-waterfall-fix

## Git Commit Message

```
feat(client): 完成 TraceExplorer 瀑布图核心功能优化

- 使用 ECharts Custom Series + renderItem 实现精确的瀑布图可视化
- 采用服务泳道（按服务合并）模式，Y 轴显示唯一服务名
- 添加完整的 X 轴交互支持（缩放、平移、滑动条）
- 使用 ResizeObserver 解决 Drawer 动画渲染问题
- 配置 filterMode: 'weakFilter' 防止长 Span 被过滤
- 实现 Tooltip 国际化支持（含 Span ID 显示）
- 优化表格列宽（Trace ID 和 Service Name 自适应）
- 添加详细注释说明 duration 为用户耗时（业务耗时）
- 创建 Phase 2 完整文档 Todo_Phase2_TraceExplorer.md
```

## Modification

### 新增文件
- `docs/todo-list/Todo_Phase2_TraceExplorer.md` - 阶段 2 完整文档
- `docs/dev-log/20251229-feat-traceexplorer-waterfall-fix.md` - 本文档

### 修改文件
- `client/src/components/TraceWaterfall.vue` - 瀑布图核心组件
  - 改用 Custom Series + renderItem 实现
  - 添加服务泳道逻辑（按服务合并）
  - 添加 X 轴 dataZoom 配置（缩放、平移、滑动条）
  - 使用 ResizeObserver 替代 window.resize
  - 使用 shallowRef 管理 ECharts 实例
  - 实现完整的国际化支持
- `client/src/components/TraceDetailDrawer.vue`
  - 移除重复的关闭按钮
  - 移除 Close 图标导入
- `client/src/components/TraceListTable.vue`
  - 优化列宽配置（Trace ID 和 Service Name 改为 min-width）
- `client/src/i18n.ts`
  - 添加瀑布图 tooltip 字段翻译（spanId, operation, startTime, duration, status）
- `client/src/views/TraceExplorer.vue`
  - 添加注释说明 duration 为用户耗时
- `client/src/types/trace.ts`
  - 在 TraceListItem 和 TraceDetail 的 duration 字段添加注释说明
- `docs/todo-list/Todo_Frontend_Refactoring.md`
  - 标记阶段 2 为已完成

## 核心技术实现

### 1. 瀑布图实现方案演进

#### 尝试 1：散点图（Scatter）
```typescript
{
  type: 'scatter',
  symbol: 'rect',
  symbolSize: (data) => [duration, 20]
}
```
**问题**：散点图符号是**中心对齐**，导致时间轴偏差
- 例如：start_time=100ms, duration=50ms
- 矩形中心在 100ms
- 实际显示：75-125ms（❌ 错误）

#### 尝试 2：堆叠条形图（Stacked Bar）
**问题**：所有条形堆叠在一起，无法实现瀑布效果

#### ✅ 最终方案：Custom Series + renderItem
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
        height: api.size([0, 1])[1] * 0.6,
        y: startPoint[1] - height / 2
      },
      style: {
        fill: getStatusColor(status),
        opacity: 0.9,
        rx: 2  // 圆角
      }
    }
  }
}
```

### 2. 服务泳道（按服务合并）

**问题**：每个 Span 独占一行，Y 轴过长（几十行）

**解决**：将 Y 轴改为唯一服务名列表
```typescript
const uniqueServices = Array.from(new Set(list.map(s => s.service_name)))
const serviceIndexMap = {}
uniqueServices.forEach((name, index) => {
  serviceIndexMap[name] = index
})

// 数据映射
data: list.map((span) => [
  serviceIndexMap[span.service_name],  // Y 轴索引（服务名）
  span.start_time,                      // 开始时间
  span.duration,                        // 持续时间
  span.status                           // 状态
])
```

**效果**：
- user-service 的所有请求显示在同一行
- order-service 的所有请求显示在同一行
- Y 轴从几十行缩短到 5-6 行

### 3. ResizeObserver 解决 Drawer 渲染问题

**问题**：el-drawer 有动画过程，初始 `display: none`
```typescript
// ❌ 不工作
window.addEventListener('resize', handleResize)
// Drawer 打开时不会触发 window resize
```

**解决**：使用 ResizeObserver 监听容器尺寸变化
```typescript
resizeObserver = new ResizeObserver(() => {
  chartInstance.value?.resize()
})
resizeObserver.observe(chartRef.value)
```

### 4. X 轴完整交互支持

```typescript
dataZoom: [
  // 底部滑动条
  {
    type: 'slider',
    xAxisIndex: 0,
    filterMode: 'weakFilter',  // 关键！防止长 Span 被过滤
    height: 16,
    bottom: 5
  },
  // 鼠标交互
  {
    type: 'inside',
    xAxisIndex: 0,
    filterMode: 'weakFilter',
    zoomOnMouseWheel: true,    // 滚轮缩放
    moveOnMouseMove: true      // 拖拽平移
  }
]
```

**关键配置**：
- `filterMode: 'weakFilter'`：只要 Span 有任何部分在视图内就显示
- 防止长耗时 Span 被错误隐藏

### 5. 国际化支持

#### 添加翻译字段
```typescript
// i18n.ts
waterfall: {
  spanId: 'Span ID',        // Span ID
  operation: 'Operation',    // 操作
  startTime: 'Start Time',   // 开始时间
  duration: 'Duration',      // 耗时
  status: 'Status'           // 状态
}
```

#### 在组件中使用
```typescript
import { useI18n } from 'vue-i18n'
const { t } = useI18n()

function renderTooltip(span: TraceSpan) {
  return `
    <div class="flex justify-between">
      <span class="text-gray-500">${t('traceExplorer.waterfall.spanId')}:</span>
      <span class="text-blue-400">${span.span_id}</span>
    </div>
    // ...
  `
}
```

### 6. 列宽优化

**问题**：Action 右侧有空白

**解决**：让 Trace ID 和 Service Name 自适应填充
```typescript
// 之前
<el-table-column prop="trace_id" width="220" />
<el-table-column prop="service_name" width="160" />

// 之后
<el-table-column prop="trace_id" min-width="240" />
<el-table-column prop="service_name" min-width="180" />
```

## 重要概念说明

### Span 状态三级分类

| 状态 | 颜色 | 含义 | 示例场景 |
|------|------|------|---------|
| **Success** | 🟢 绿色 | 一切正常 | HTTP 200, SQL 查询快速 |
| **Warning** | 🟡 黄色 | 成功但有隐患 | 慢查询（>2s）、重试成功、降级服务 |
| **Error** | 🔴 红色 | 请求失败 | HTTP 500, 超时、异常 |

**为什么需要 Warning？**
在微服务架构中，"慢"往往比"错"更致命：
- 错误 = 明确的失败，容易定位
- 慢 = 隐形的问题，影响用户体验但难排查

### Trace Duration（用户耗时）

**定义**：用户服务从请求开始到结束的真实墙钟时间

**计算**：
```javascript
duration = max(spans.map(s => s.start_time + s.duration))
         - min(spans.map(s => s.start_time))
```

**不包含**：
- LogSentinel 的分析耗时
- LogSentinel 的等待超时时间

**为什么？**
- 用户关心的是"我的服务有多慢"
- 不是"你分析用了多久"

## Learning Tips

### Newbie Tips
1. **ECharts Custom Series**：`renderItem` 允许手动计算每个数据点的渲染位置和形状
   - `api.coord()`：将数据坐标转换为像素坐标
   - `api.size()`：获取坐标轴的尺寸

2. **ResizeObserver**：监听元素尺寸变化的现代 API
   - 比 `window.resize` 更精确
   - 适合监听容器元素（如 Drawer）

3. **服务泳道**：将相同类型的数据归类到同一行
   - 适合展示高重复数据的分布
   - 减少 Y 轴长度，提升可读性

### Function Explanation
- `api.coord([value, categoryIndex])`：将数据值转换为屏幕像素坐标
- `filterMode: 'weakFilter'`：数据只要部分在视图内就显示
- `shallowRef`：Vue 3 的浅层响应式引用，避免深度代理大型对象
- `ResizeObserver`：监听元素内容区域或边框盒尺寸变化

### Pitfalls
1. **Scatter 图的中心对齐**：散点图的符号默认是中心对齐，不适合需要精确对齐的条形图
2. **Drawer 动画问题**：el-drawer 的动画过程会导致 ECharts 获取不到正确的容器尺寸
3. **dataZoom 的 filterMode**：默认模式下，长 Span 会被错误过滤，必须使用 `weakFilter`
4. **ref vs shallowRef**：ECharts 实例很大，使用 `ref` 会导致性能问题，应使用 `shallowRef`

## 设计亮点

### 为什么使用 Custom Series 而不是 Bar Chart？

**Bar Chart 的问题**：
- 每个条形必须从 0 开始（或堆叠）
- 无法实现"起点不同，宽度不同"的瀑布效果

**Custom Series 的优势**：
- 完全控制每个矩形的 x, y, width, height
- 可以精确对齐时间轴
- 支持任意复杂的可视化需求

### 为什么使用服务泳道而不是每个 Span 一行？

**问题**：一个 Trace 可能有 50 个 Span，Y 轴过长
- 需要大量滚动
- 无法快速看出哪些服务在忙碌

**解决**：按服务合并
- Y 轴只有 5-6 行（服务数量）
- 可以看出每个服务在不同时间段的活跃度
- 便于识别服务调用模式

### 为什么 X 轴交互如此重要？

**Trace 的特点**：
- 可能有几百个 Span
- 耗时从几毫秒到几秒不等
- 需要同时看全局和细节

**X 轴交互的价值**：
- 滚轮缩放：从全局（1 秒）到细节（1 毫秒）
- 拖拽平移：快速浏览不同时间段
- 滑动条：直观显示当前视图范围

## 遇到的问题和解决

### 问题 1：瀑布图条形起点都对齐在 0
**原因**：错误使用了 scatter 散点图，符号是中心对齐
**解决**：改用 Custom Series + renderItem 手动计算矩形坐标

### 问题 2：Y 轴太长（几十个 Span）
**原因**：每个 Span 独占一行
**解决**：改为服务泳道（按服务合并），Y 轴只显示唯一服务名

### 问题 3：Drawer 中瀑布图宽度为 0
**原因**：el-drawer 有动画，初始 `display: none`，window.resize 无法触发
**解决**：使用 ResizeObserver 监听容器尺寸变化

### 问题 4：长 Span 被 dataZoom 过滤掉
**原因**：默认 filterMode 会在 Span 中心不在视图内时隐藏它
**解决**：使用 `filterMode: 'weakFilter'`

### 问题 5：两个关闭按钮
**原因**：Element Plus Drawer 自带关闭按钮，又手动添加了一个
**解决**：移除手动添加的关闭按钮和 Close 图标导入

### 问题 6：瀑布图"固定不动"
**原因**：没有配置 X 轴的 dataZoom
**解决**：添加 X 轴的 slider 和 inside dataZoom

## 下一步计划

- [ ] 阶段 3：AIEngine（AI 引擎中心）- 技术展示
- [ ] 阶段 4：SystemStatus（系统状态）- 精简
- [ ] 测试所有页面的响应式布局
- [ ] 性能优化（懒加载、虚拟滚动）
