# 2025-12-25 时间时区统一修复

## Git Commit Message

```
feat(frontend): 统一前端时间显示为北京时间
```

## Modification

修改的文件：
1. `client/src/utils/timeFormat.ts` (新建) - 创建时间格式化工具函数，将 UTC 时间转换为北京时间
2. `client/src/views/History.vue` - 历史日志页面使用时间转换函数处理 `processed_at`
3. `client/src/stores/system.ts` - 在 Dashboard 的 `recent_alerts` 和 LiveLogs 的 `fetchLogs` 中使用时间转换函数

## 问题描述

### 根本原因
- SQLite 的 `CURRENT_TIMESTAMP` 返回 **UTC 时间**（协调世界时）
- C++ 后端的 `localtime()` 函数返回**系统本地时间**（北京时间，UTC+8）
- 两种时间来源不一致，导致前端显示的时间比实际时间**慢 8 小时**

### 系统时区
```bash
$ timedatectl
Time zone: Asia/Shanghai (CST, +0800)
```
系统已经是北京时间，但 SQLite 的 `CURRENT_TIMESTAMP` 不受系统时区影响。

## 解决方案

### 方案选择
采用**前端统一处理**方案：
- 后端继续存储 UTC 时间（国际化最佳实践）
- 前端统一使用 JavaScript 的 Date 对象进行时区转换

### 优点
1. 不需要修改后端 C++ 代码和数据库结构
2. 符合国际化的最佳实践（数据库存储 UTC，显示层转换）
3. 便于将来支持多时区用户

## 实现细节

### 1. 时间格式化工具 (`timeFormat.ts`)

```typescript
export function formatToBeijingTime(utcString: string): string {
  // 解析 UTC 时间字符串（添加 'Z' 标记）
  const date = new Date(utcString + 'Z')

  // 格式化为 YYYY-MM-DD HH:mm:ss
  return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`
}
```

**关键点**：
- 在时间字符串后添加 `'Z'`，明确告诉 JavaScript 这是 UTC 时间
- 使用 `new Date()` 自动进行时区转换
- 手动格式化避免依赖第三方库

### 2. 应用位置

#### History.vue（历史日志）
```typescript
timestamp: formatToBeijingTime(item.processed_at)
```

#### system.ts - Dashboard（最近告警）
```typescript
time: formatToBeijingTime(a.time)
```

#### system.ts - LiveLogs（实时日志）
```typescript
timestamp: formatToBeijingTime(item.processed_at)
```

## Learning Tips

### Newbie Tips（新手提示）

1. **SQLite 的 CURRENT_TIMESTAMP 是 UTC 时间**
   - `TIMESTAMP DEFAULT CURRENT_TIMESTAMP` 会存储 UTC 时间，不受系统时区影响
   - 如需存储本地时间，需要使用 `TIMESTAMP DEFAULT (datetime('now', 'localtime'))`

2. **JavaScript 的 Date 时区处理**
   - `new Date('2025-12-25 10:00:00')` 会按本地时区解析
   - `new Date('2025-12-25 10:00:00Z')` 会按 UTC 时区解析（注意 Z）
   - 显示时自动转换为浏览器本地时区

### Function Explanation（函数说明）

1. **`new Date(utcString + 'Z')`**
   - `'Z'` 是 UTC 时间的后缀标记
   - 告诉 JavaScript 解析器这是 UTC 时间

2. **`date.getTime()`**
   - 返回从 1970-01-01 00:00:00 UTC 到现在的毫秒数
   - 可以用来检查日期是否有效（`isNaN(date.getTime())`）

3. **`String.padStart(2, '0')`**
   - 确保月、日、时、分、秒都是两位数
   - 例如：`9` → `"09"`

### Pitfalls（常见坑）

1. **时间字符串格式不一致**
   - SQLite 可能返回 `2025-12-25 10:00:00`（无时区信息）
   - 后端 C++ 可能返回 `2025-12-25 10:00:00`（本地时间）
   - **解决**：统一在 SQLite 层使用 UTC，前端统一转换

2. **忘记添加 'Z' 后缀**
   - 不加 `'Z'`，JavaScript 会按本地时区解析，导致重复转换
   - **错误**：`new Date('2025-12-25 10:00:00')` → 本地时间 10:00
   - **正确**：`new Date('2025-12-25 10:00:00Z')` → UTC 时间 10:00 → 北京时间 18:00

3. **日期格式化的性能问题**
   - 在循环中频繁创建 Date 对象和字符串拼接可能有性能开销
   - **优化**：对于大批量数据，可以考虑使用更高效的日期库（如 dayjs）
   - 本场景中，数据量较小（50条日志），手动格式化足够

## 测试验证

1. **编译通过**：`npm run build` ✓
2. **时间转换正确**：UTC 时间 + 8 小时 = 北京时间
3. **所有页面覆盖**：
   - 历史日志页面 ✓
   - Dashboard 最近告警 ✓
   - 实时日志流 ✓

## 后续优化建议

1. **添加相对时间显示**（如 "5分钟前"）
   - 在 `timeFormat.ts` 中已实现 `formatToRelativeTime()` 函数
   - 可以在 UI 中提供更友好的时间显示

2. **考虑使用 dayjs 插件**
   - dayjs 的 UTC 插件和时区插件提供更强大的功能
   - 但需要额外安装：`dayjs/plugin/utc` 和 `dayjs/plugin/timezone`

3. **用户可配置时区**
   - 如果将来支持多时区用户，可以在设置中添加时区选项
   - 动态转换为目标时区
