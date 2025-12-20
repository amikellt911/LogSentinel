# 前端 LogEntry 类型定义释义

本文档旨在解释 `client/src/stores/system.ts` 中 `LogEntry` 接口为何包含多种看似重复或不一致的 `level` 类型。

## 类型定义

```typescript
export interface LogEntry {
  id: number | string
  timestamp: string
  level: 'INFO' | 'WARN' | 'RISK' | 'Critical' | 'Error' | 'Warning' | 'Info' | 'Safe'
  message: string
}
```

## 核心原因：兼容不同数据源与模式

这个类型定义是一个 **联合类型 (Union Type)**，它的存在是为了同时兼容系统中的两套不同的数据流标准。

### 1. 模拟模式与实时流标准 (`INFO` | `WARN` | `RISK`)

这三个大写简写值主要来源于前端的 **模拟数据生成器 (Mock Generator)** 和 **旧版实时日志流** 逻辑。

*   **来源**: `client/src/stores/system.ts` 中的 `generateRandomLog()` 函数。
*   **场景**:
    *   当后端未连接或处于 "Simulation Mode" 时，前端会自动生成演示用的日志。
    *   为了简化模拟逻辑，代码中只定义了三种简单的状态：普通信息 (`INFO`)、警告 (`WARN`)、风险 (`RISK`)。
    *   `LiveLogs.vue` (实时日志视图) 在某些情况下直接消费这些数据。

### 2. 真实后端标准 (`Critical` | `Error` | `Warning` | `Info` | `Safe`)

这五个值是 **C++ 后端 API** 返回的标准风险等级（Risk Levels），也是目前系统推荐的规范数据。

*   **来源**: 后端 `/api/history` 接口返回的 JSON 数据。
*   **场景**:
    *   `History.vue` (历史日志视图) 从后端拉取真实日志。
    *   为了在界面上展示更丰富、准确的分类（如区分 `Critical` 严重错误和 `Error` 一般错误），历史页面使用了全量的标准等级。
    *   UI 组件（如表格中的标签）会根据这些值渲染不同的颜色（红、橙、黄、蓝、绿）。

## 为什么不统一？

这是一个 **过渡期的设计 (Transitional Design)**。

1.  **历史包袱**: 早期开发阶段仅使用了简单的 `RISK/WARN/INFO` 进行原型验证。
2.  **Mock 数据复杂性**: 模拟数据生成器尚未完全重构以支持 5 层细粒度的风险模拟，因此仍保留了旧的三层分类。
3.  **类型安全**: 为了让 TypeScript 编译器在 `History.vue` (使用新标准) 和 `system.ts` (使用旧标准) 中都不报错，`LogEntry` 接口必须宽容地接受这两种集合的并集。

## 前端展示处理

UI 组件通常会通过辅助函数（Helper Functions）将这些异构的类型映射到统一的样式上。例如在 `History.vue` 中：

*   `RISK`, `Critical`, `High` -> **红色 (Red)**
*   `WARN`, `Error`, `Medium` -> **橙色 (Orange)**
*   `Warning`, `Low` -> **黄色 (Yellow)**
*   `INFO`, `Info` -> **蓝色/灰色 (Blue/Gray)**
*   `Safe` -> **绿色 (Green)**

## 总结

这个 `level` 字段是 **给前端展示使用的**，它作为一个“兼容层”，确保无论是来自 Mock 引擎的简单数据，还是来自真实后端的精细数据，都能在同一个表格组件中被正确渲染，而不会导致类型错误或页面崩溃。
