# 2025-12-25 修复实时日志流等级显示问题

## Git Commit Message

```
fix(frontend): 修复实时日志流等级显示错误，统一使用标准等级
```

## Modification

修改的文件：
1. `client/src/stores/system.ts`
   - 更新 `LogEntry` 接口的 `level` 类型定义
   - 修复 `fetchLogs()` 函数的等级映射逻辑，使用大小写不敏感的比较
   - 更新 `generateRandomLog()` 函数的等级生成逻辑
   - 更新 `LOG_MESSAGES` 和 `LOG_MESSAGES_ZH` 对象的 key

2. `client/src/views/LiveLogs.vue`
   - 为每个等级设计独立的背景和文字颜色样式
   - 移除旧的简写等级（INFO, WARN, RISK），使用标准等级

## 问题描述

### 问题现象
实时日志流在系统运行时（非模拟模式）下，所有日志都显示为 Info 等级，而实际后端返回的日志等级各不相同。

模拟模式下显示正常，因为模拟数据直接使用前端定义的等级。

### 根本原因

在 `system.ts` 的 `fetchLogs()` 函数中，等级判断逻辑使用了**大小写敏感**的比较：

```typescript
// 错误的代码
if (item.risk_level === 'Critical' || item.risk_level === 'High') level = 'RISK';
else if (item.risk_level === 'Warning' || item.risk_level === 'Medium') level = 'WARN';
```

后端返回的是**小写**的等级值（`'critical'`, `'error'`, `'warning'`），而代码判断的是**首字母大写**的值（`'Critical'`, `'Warning'`），导致判断失败，所有日志都显示为默认的 'INFO'（现在的 'Info'）等级。

对比 `History.vue` 中的正确实现：
```typescript
// 正确的代码（History.vue）
const lower = level.toLowerCase();
if (lower === 'critical' || lower === 'high') level = 'Critical';
```

## 解决方案

### 1. 统一等级标准

将前端所有页面的等级统一为 6 个标准等级：
- **Critical** - 严重（红色）
- **Error** - 错误（橙色）
- **Warning** - 警告（黄色）
- **Info** - 信息（蓝色）
- **Safe** - 安全（绿色）
- **Unknown** - 未知（灰色）

### 2. 修复等级映射逻辑

在 `fetchLogs()` 函数中，使用和 `History.vue` 相同的等级映射逻辑：

```typescript
let level = item.risk_level;
const lower = level.toLowerCase();
if (lower === 'critical' || lower === 'high') level = 'Critical';
else if (lower === 'error' || lower === 'medium') level = 'Error';
else if (lower === 'warning' || lower === 'low') level = 'Warning';
else if (lower === 'info') level = 'Info';
else if (lower === 'safe') level = 'Safe';
else if (lower === 'unknown') level = 'Unknown';
```

### 3. 更新 LiveLogs.vue 样式

为每个等级设计独立的颜色方案：

| 等级 | 背景色 | 文字颜色 | 标签颜色 |
|------|--------|----------|----------|
| Critical | `bg-red-900/20` + 红色边框 | `text-red-100` | `text-red-500` |
| Error | `bg-orange-900/20` + 橙色边框 | `text-orange-100` | `text-orange-500` |
| Warning | `bg-yellow-900/20` + 黄色边框 | `text-yellow-100` | `text-yellow-500` |
| Info | `bg-blue-900/20` + 蓝色边框 | `text-blue-100` | `text-blue-400` |
| Safe | `bg-green-900/20` + 绿色边框 | `text-green-100` | `text-green-500` |
| Unknown | `bg-gray-800/50` + 灰色边框 | `text-gray-400` | `text-gray-500` |

## Learning Tips

### Newbie Tips（新手提示）

1. **大小写敏感的比较是常见的 bug 来源**
   - 后端返回的数据可能是小写、大写或混合大小写
   - 前端比较时应该统一转换为小写或大写后再比较
   - **推荐**：使用 `toLowerCase()` 进行大小写不敏感的比较

2. **类型定义的一致性**
   - TypeScript 的类型定义应该与实际数据结构保持一致
   - 修改类型时，要检查所有使用该类型的地方
   - 本例中：修改 `LogEntry.level` 类型后，需要更新：
     - 模拟数据生成
     - 消息模板对象的 key
     - UI 样式判断逻辑

3. **Vue 动态 class 绑定的两种样式**
   - 整行样式（背景、边框、整体文字颜色）
   - 标签样式（单独某个元素的字体颜色）
   - 两者可以独立设置，互不影响

### Function Explanation（函数说明）

1. **`toLowerCase()`**
   - 将字符串转换为小写
   - 用于大小写不敏感的字符串比较
   - **示例**：`'Critical'.toLowerCase() === 'critical'`

2. **Vue 的 `:class` 对象语法**
   ```vue
   <div :class="{
     'class-a': condition1,
     'class-b': condition2
   }">
   ```
   - 当 `condition1` 为 true 时，应用 `class-a`
   - 多个条件可以同时满足，多个 class 会同时应用

### Pitfalls（常见坑）

1. **类型定义与实际数据不匹配**
   - **问题**：TypeScript 类型定义了 `'INFO' | 'WARN' | 'RISK'`，但实际数据使用 `'Critical' | 'Error' | 'Warning'`
   - **后果**：类型检查通过，但运行时逻辑错误
   - **解决**：确保类型定义与实际数据结构一致

2. **模拟数据与真实数据结构不同**
   - **问题**：模拟模式使用前端生成的数据（等级正确），真实模式使用后端返回的数据（等级映射失败）
   - **后果**：模拟模式正常，生产环境出 bug
   - **解决**：确保模拟数据的结构与后端返回的数据结构一致

3. **样式的重复和冲突**
   - **问题**：多个等级共享同一种颜色（Info, Safe, Unknown 都用灰色背景）
   - **后果**：用户体验差，难以快速区分不同等级
   - **解决**：为每个等级设计独立的颜色方案

4. **字符串比较时的拼写错误**
   - **问题**：`'Critical'` vs `'critical'`，`'Warn'` vs `'Warning'`
   - **后果**：比较失败，条件分支不执行
   - **解决**：使用常量或枚举定义等级名称，避免硬编码字符串

## 测试验证

1. **类型检查通过**：`npm run build` ✓
2. **模拟模式**：所有 6 个等级都能正确生成和显示 ✓
3. **真实模式**：后端返回的日志等级能正确映射和显示 ✓
4. **颜色区分**：每个等级都有独立的视觉样式 ✓

## 后续优化建议

1. **使用枚举或常量定义等级**
   ```typescript
   export enum LogLevel {
     CRITICAL = 'Critical',
     ERROR = 'Error',
     WARNING = 'Warning',
     INFO = 'Info',
     SAFE = 'Safe',
     UNKNOWN = 'Unknown'
   }
   ```
   - 避免字符串拼写错误
   - 提供类型安全
   - 便于重构

2. **统一等级映射逻辑**
   - 目前在 `History.vue` 和 `system.ts` 中有重复的映射逻辑
   - 可以抽取为公共函数 `normalizeRiskLevel()`
   - 确保所有页面使用相同的映射规则

3. **添加单元测试**
   - 测试等级映射函数覆盖所有后端返回值
   - 测试边界情况（未知等级、空字符串等）
