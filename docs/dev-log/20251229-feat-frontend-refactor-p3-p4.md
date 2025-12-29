# 2025-12-29 - feat(client): 前端架构重构 Phase 3 & 4 - AI 引擎与系统状态

## Git Commit Message

feat(client): 完成前端架构重构 Phase 3 (AI引擎) 和 Phase 4 (系统状态)

- 新增 `AIEngine.vue`：展示 Token 消耗、节省比例及历史批次存档
- 新增 `SystemStatus.vue`：精简版仪表盘，专注于系统核心指标
- 新增 `PromptDebugger.vue`：支持开发者查看 AI Prompt 输入输出详情
- 新增 `TokenMetricsCard.vue`：用于展示 AI 相关的核心指标
- 优化 `MainLayout.vue`：更新导航菜单，整合新页面路由
- 完善国际化：添加 AI 引擎和系统状态相关的中英文翻译
- 修复 TypeScript 类型检查错误，确保构建通过

## Modification

- `client/src/views/AIEngine.vue` (New)
- `client/src/views/SystemStatus.vue` (New)
- `client/src/components/BatchArchiveList.vue` (New)
- `client/src/components/PromptDebugger.vue` (New)
- `client/src/components/TokenMetricsCard.vue` (New)
- `client/src/types/aiEngine.ts` (New)
- `client/src/router/index.ts` (Modified)
- `client/src/layout/MainLayout.vue` (Modified)
- `client/src/i18n.ts` (Modified)

## Learning Tips

### Newbie Tips
- **Vue 3 Composition API**: 在 `<script setup>` 中使用 `defineProps` 和 `defineEmits` 时不需要手动导入，但在 TypeScript 环境下最好使用 `withDefaults` 来设置默认值。
- **TypeScript `import type`**: 当启用了 `verbatimModuleSyntax` 或类似严格配置时，导入接口必须使用 `import type { InterfaceName }`，否则构建会报错。
- **ECharts ResizeObserver**: 在 Vue 组件中，如果图表所在的容器（如 Drawer 或 Tab）初始状态是隐藏的，直接 `init` 会导致宽度计算为 0。使用 `ResizeObserver` 监听容器大小变化是一个稳健的解决方案。

### Function Explanation
- **`navigator.clipboard.writeText()`**: 原生浏览器 API，用于实现"复制到剪贴板"功能，非常方便，但在非 HTTPS 环境下可能受限（localhost 除外）。
- **`JSON.stringify(obj, null, 2)`**: 快速格式化 JSON 输出，第三个参数 `2` 表示缩进 2 个空格，非常适合用于 Debug 视图展示。

### Pitfalls
- **文件系统状态同步**: 在极少数情况下，工具报告的文件列表可能与实际文件系统状态不一致（如本次遇到的文件消失问题）。在重构或大量创建文件时，务必多次验证文件是否存在。
- **Playwright 截图验证**: 虽然自动化脚本运行成功并生成了截图，但这并不代表代码本身没有 Bug（如 TypeScript 类型错误）。必须结合 `npm run build` 进行编译检查。
- **Store 类型推断**: Pinia Store 的类型推断在复杂项目中可能会失效，导致在组件中使用 Store 属性时报错（如 `Property 'stats' does not exist`）。使用 `// @ts-ignore` 是临时规避方案，长期应完善 Store 的类型定义。
