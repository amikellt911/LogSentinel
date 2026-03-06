# 20251229-feat-traceexplorer.md

## Git Commit Message

```
feat(client): 新增 TraceExplorer Trace 追溯页面

- 创建 TraceExplorer.vue 页面，替换原 History.vue，实现分布式追溯功能
- 创建 TraceSearchBar.vue 组件，支持按 trace_id、service_name、time_range、risk_level、min_duration 搜索
- 创建 TraceListTable.vue 组件，展示 Trace 列表（支持分页、排序、行点击）
- 创建 TraceWaterfall.vue 组件，使用 ECharts 堆叠条形图实现瀑布图可视化（核心功能）
- 创建 TraceDetailDrawer.vue 组件，展示 AI 根因分析、瀑布图、Span 详细列表
- 定义 TypeScript 类型（TraceListItem、TraceDetail、TraceSpan、AIAnalysis、TraceSearchCriteria）
- 添加国际化翻译（中英文），更新路由和导航菜单（/history → /traces）
- 所有组件使用 Mock 数据，预留 API 对接点
```

## Modification

### 新增文件
- `client/src/types/trace.ts` - Trace 相关 TypeScript 类型定义
- `client/src/views/TraceExplorer.vue` - Trace 追溯主页
- `client/src/components/TraceSearchBar.vue` - 搜索表单组件
- `client/src/components/TraceListTable.vue` - Trace 列表表格组件
- `client/src/components/TraceWaterfall.vue` - 瀑布图可视化组件（ECharts）
- `client/src/components/TraceDetailDrawer.vue` - 详情抽屉组件

### 修改文件
- `client/src/i18n.ts` - 添加 traceExplorer 相关的中英文翻译
- `client/src/router/index.ts` - 将 /history 路由改为 /traces，导入 TraceExplorer
- `client/src/layout/MainLayout.vue` - 更新导航菜单（/history → /traces）

## 核心功能实现

### 1. 搜索功能
- **精确匹配**：Trace ID 输入框
- **下拉选择**：服务名称（user-service、order-service 等）
- **快捷时间范围**：1h/6h/24h/自定义
- **风险等级多选**：Critical/Error/Warning/Info/Safe
- **耗时过滤**：最小耗时（ms）
- **重置按钮**：清空所有搜索条件

### 2. Trace 列表
- **表格列**：trace_id、service_name、start_time、duration、span_count、risk_level、操作
- **分页组件**：支持 20/50/100 每页数量
- **排序功能**：按开始时间、耗时、Span 数量排序
- **行点击事件**：点击任意行打开详情抽屉
- **耗时颜色**：>1000ms 红色，>500ms 黄色，正常灰色
- **风险等级徽章**：Critical（红）、Error（橙）、Warning（黄）、Info（蓝）、Safe（绿）

### 3. 瀑布图可视化（核心！）
- **技术方案**：ECharts 堆叠条形图（Stacked Bar）
- **X 轴**：时间线（相对时间 0ms ~ 总耗时）
- **Y 轴**：服务名称（按调用顺序排序）
- **数据格式**：`[start_time, duration]` 实现瀑布效果
- **颜色映射**：success（绿）、error（红）、warning（黄）
- **交互功能**：
  - Hover 显示详细信息（Tooltip）
  - dataZoom 支持缩放
  - barMinWidth: 2 防止细条看不见

### 4. 详情抽屉
- **布局结构**：
  - **顶部**：AI 根因分析（风险等级徽章、总结、根因、解决方案）
  - **中部**：瀑布图（调用链可视化）
  - **底部**：Span 详细列表（可折叠）
- **抽屉特性**：
  - 从右侧滑出
  - 宽度自适应（桌面端 70%，平板 80%，移动端 100%）
  - 支持全屏切换
  - 关闭时清空数据（释放内存）

## Learning Tips

### Newbie Tips
1. **ECharts 瀑布图原理**：使用 `[start_time, duration]` 数据格式实现瀑布效果
   - 第一维：开始时间（透明占位）
   - 第二维：实际耗时（带颜色）
2. **Element Plus Drawer**：从四个方向滑出（rtl/ltr/ttb/btt），支持自定义宽度
3. **TypeScript 类型定义**：集中管理类型（types/trace.ts），提高代码可维护性

### Function Explanation
- `transformSpansToChartData()` - 将 TraceSpan 数组转换为 ECharts 数据格式
- `barMinWidth: 2` - ECharts 配置，防止细条看不见
- `dataZoom` - ECharts 数据缩放组件，支持鼠标滚轮缩放
- `v-model` - Vue 3 双向绑定，用于抽屉显示/隐藏

### Pitfalls
1. **ECharts 内存泄漏**：`onUnmounted` 时必须调用 `dispose()` 释放内存
2. **抽屉宽度响应式**：使用 `window.innerWidth` 判断，监听 resize 事件
3. **路由名称变更**：记得更新 MainLayout.vue 中的 `currentRouteName` 映射
4. **Mock 数据一致性**：确保列表和详情的 trace_id 一致

## 设计亮点

### 为什么使用堆叠条形图而不是 Timeline？
- **Timeline**：适合展示绝对时间点（如 Gantt 图）
- **堆叠条形图**：适合展示相对时间（瀑布流），每个服务的调用关系更清晰
- **数据格式**：`[start_time, duration]` 实现了"延迟 + 耗时"的瀑布效果

### 为什么抽屉宽度是 70% 而不是 50%？
- **瀑布图需要空间**：X 轴是时间线，需要足够的宽度展示细节
- **响应式考虑**：移动端 100%，平板 80%，桌面端 70%
- **用户体验**：70% 可以同时看到抽屉和部分列表，方便切换

### 为什么 Span 列表默认展开？
- **可调试性**：开发者可以直接查看所有 Span 的详细信息
- **快速扫描**：一眼看出哪些 Span 有问题（红色/黄色）
- **可折叠**：用户可以根据需要折叠，节省空间

## 下一步计划

- [ ] 阶段 3：AIEngine（AI 引擎中心）- 技术展示
- [ ] 阶段 4：SystemStatus（系统状态）- 精简
- [ ] 测试 TraceExplorer 响应式布局
- [ ] 优化瀑布图性能（大数据量）
