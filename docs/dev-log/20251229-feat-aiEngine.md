# 20251229-feat-aiEngine.md

## Git Commit Message

```
feat(client): 新增 AIEngine AI 引擎中心页面

实现了 Token 消耗监控、历史批次存档管理和 Prompt 透视功能。
包含 TokenMetricsCard、BatchArchiveList、PromptDebugger 三个核心组件，
使用 Mock 数据进行演示，预留了 API 对接点。
```

## Modification

### 新增文件
- `client/src/components/TokenMetricsCard.vue` - Token 指标卡片组件（4 个指标）
- `client/src/components/BatchArchiveList.vue` - 历史批次存档列表组件（可展开详情）
- `client/src/components/PromptDebugger.vue` - Prompt 透视抽屉组件（JSON 高亮显示）
- `client/src/views/AIEngine.vue` - AI 引擎中心主页面

### 修改文件
- `client/src/i18n.ts` - 添加 `aiEngine` 和 `common` 翻译（中英文）

## Learning Tips

### Newbie Tips

1. **Element Plus 表格展开功能**：
   - 使用 `type="expand"` 定义展开列
   - 通过 `@expand-change` 监听展开事件
   - 可以自定义展开区域的内容

2. **JSON 语法高亮的简单实现**：
   - 不需要引入大型库（如 Prism.js）
   - 使用 Tailwind CSS 类 + 正则替换即可实现简单高亮
   - 代码量少，性能好

3. **抽屉组件（Drawer）的使用**：
   - 使用 `v-model:visible` 控制显示/隐藏
   - `direction="rtl"` 从右侧滑出
   - `:size` 可以是百分比（自适应屏幕）

### Function Explanation

1. **`navigator.clipboard.writeText()`**：
   - 现代浏览器提供的剪贴板 API
   - 返回 Promise，异步执行
   - 需要 HTTPS 或 localhost 环境

2. **`v-html` 指令**：
   - 用于渲染包含 HTML 的字符串
   - 配合 `syntaxHighlight` 函数实现 JSON 高亮
   - 注意 XSS 风险（本场景中仅显示 JSON，风险可控）

3. **`computed` 属性**：
   - 响应式计算属性
   - 用于动态生成抽屉宽度、高亮 JSON 等
   - 依赖项变化时自动重新计算

### Pitfalls

1. **在 `<script setup>` 中使用 `$t`**：
   - 错误：直接使用 `$t('common.copy')`
   - 正确：`const { t } = useI18n()` 然后 `t('common.copy')`
   - 原因：`$t` 是 Options API 的写法，Composition API 需要调用 `useI18n()`

2. **Element Plus 表格样式覆盖**：
   - 需要使用 `:deep()` 或 `::v-deep` 选择器
   - 表格背景色需要同时设置 `tr` 和 `td`
   - hover 效果需要 `!important` 提高优先级

3. **展开行与按钮点击冲突**：
   - 点击"查看详情"按钮时，会同时触发展开
   - 解决方案：使用 `expandedRows` Set 手动管理展开状态
   - 避免状态不同步

4. **JSON 高亮中的 XSS 风险**：
   - 在 `v-html` 中渲染用户输入的内容时，需要先转义
   - 本组件中只渲染 Mock 数据，风险可控
   - 生产环境中建议使用 `DOMPurify` 等库进行清理

---

**开发完成时间**：2025-12-29
**下一个阶段**：SystemStatus（系统状态精简）+ 路由导航调整
