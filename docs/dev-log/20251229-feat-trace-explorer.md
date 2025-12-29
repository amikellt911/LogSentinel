# 2025-12-29 - Phase 4: TraceExplorer 增强 & UI 优化

## Git Commit Message
```
feat(frontend): TraceExplorer 增强 & UI 优化

- 将 TokenMetricsCard 组件迁移至 SystemStatus 页面
- TraceExplorer 表格新增 Token 列显示
- 拆分 Actions 为三个独立按钮（AI 分析、调用链、Prompt 透视）
- 移除 AIEngine 路由和导航菜单项
- 将 "Dashboard" 重命名为 "System Status"
- Live Logs 新增 trace_id 和 span_id 显示
- 简化 Settings 提示词列表，移除 Map/Reduce 类型标签显示
- 修复 PromptDebugger v-model 双向绑定问题
- 新增 AIAnalysisDrawerContent 和 CallChainDrawerContent 组件
```

## Modification

### 新增文件 (2)
- `client/src/components/AIAnalysisDrawerContent.vue` - AI 根因分析抽屉内容组件
- `client/src/components/CallChainDrawerContent.vue` - 调用链瀑布图抽屉内容组件

### 修改文件 (11)
- `client/src/views/Dashboard.vue` - 集成 TokenMetricsCard
- `client/src/components/TraceListTable.vue` - 添加 Token 列，拆分操作按钮
- `client/src/types/trace.ts` - 新增 token_count 和 PromptDebugInfo 类型定义
- `client/src/views/TraceExplorer.vue` - 三抽屉逻辑处理
- `client/src/components/PromptDebugger.vue` - 修复 v-model 绑定
- `client/src/router/index.ts` - 移除 AIEngine 路由
- `client/src/layout/MainLayout.vue` - 移除 AI Engine 菜单项
- `client/src/i18n.ts` - 重命名 Dashboard，新增翻译键
- `client/src/stores/system.ts` - LogEntry 接口新增 trace_id/span_id
- `client/src/views/LiveLogs.vue` - 显示 trace_id 和 span_id
- `client/src/views/Settings.vue` - 简化提示词列表，移除类型标签

## Learning Tips

### Newbie Tips

1. **组件拆分的最佳实践**
   - 当单个组件承担过多职责时，应拆分为多个子组件
   - 本例中将 TraceExplorer 的三种抽屉内容拆分为独立组件，便于复用和维护
   - 拆分时通过 props 传递数据，通过 emit 触发父组件事件

2. **Element Plus Drawer 的双向绑定约定**
   - Element Plus 的 `el-drawer` 组件使用 `v-model` 或 `:model-value` + `@update:model-value` 模式
   - 注意不要使用 `visible` prop，这是旧版 API
   - 正确用法：`<el-drawer :model-value="visible" @update:model-value="$emit('update:modelValue', $event)" />`

3. **TypeScript 类型共享**
   - 在 `types/` 目录下定义类型接口，多个组件共享
   - 例如 `PromptDebugInfo` 在 PromptDebugger.vue 和 trace.ts 中共享
   - 类型定义使用 `export interface` 便于跨文件导入

### Function Explanation

1. **Vue 3 Composition API 的 `computed`**
   - `computed()` 创建计算属性，基于响应式依赖自动更新
   - 示例：`const selectedPrompt = computed(() => prompts.find(p => p.id === selectedId))`
   - 计算属性有缓存，只有依赖变化时才重新计算

2. **Element Plus 的 `el-drawer` 组件**
   - 抽屉式侧边栏，适合显示详细信息
   - `direction` 属性控制方向：`rtl`（从右往左）、`ltr`（从左往右）
   - `size` 属性可以设置宽度（百分比或像素）

3. **Tailwind CSS 的 `truncate` 工具类**
   - `truncate`：单行文本溢出显示省略号
   - 需配合 `overflow-hidden` 和 `whitespace-nowrap` 使用
   - 多行省略可用 `line-clamp-n`

### Pitfalls

1. **不要混用 Element Plus 的 v-model API**
   - Element Plus 早期版本使用 `:visible` 和 `@update:visible`
   - 新版本统一使用 `:model-value` 和 `@update:model-value`
   - 混用会导致双向绑定失效，抽屉无法关闭

2. **TypeScript 类型定义要完整**
   - 接口新增字段后，所有使用该接口的地方都要更新
   - 例如 `LogEntry` 新增 `trace_id` 后，`generateRandomLog()` 也需要生成该字段
   - 否则会导致运行时 `undefined` 错误

3. **i18n 翻译键的层级结构**
   - 翻译键采用点分隔路径：`traceExplorer.table.tokenCount`
   - 在模板中使用 `$t()` 访问，在 script 中使用 `t()` 访问
   - 新增翻译键后，要在所有语言（en/zh）中都添加对应值

4. **Mock 数据生成要符合类型定义**
   - 使用 TypeScript 后，Mock 数据必须严格匹配接口类型
   - 例如 `token_count` 不能为 `null` 或 `undefined`
   - 建议在开发阶段开启 `strictNullChecks` 检查

## 技术栈总结

- **Vue 3** Composition API + `<script setup>` 语法
- **TypeScript** 类型系统和接口定义
- **Element Plus** UI 组件库（el-drawer, el-table, el-button, el-tooltip）
- **Vue I18n** 国际化（EN/ZH）
- **Tailwind CSS** 原子化 CSS 框架
- **Pinia** 状态管理（useSystemStore）
