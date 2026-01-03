# 20251230-feat-benchmark

## Git Commit Message

```
feat(frontend): 重构基准测试页面，新增 QPS 测试与开销分析双模式

- 添加 Tab 切换功能，分离 QPS 测试与开销分析
- QPS 测试：移除 net_io 和 scheduler 指标，聚焦性能核心指标
- 开销分析：新增 AI 调用次数、Token 开销、预计花费对比
- 价格参考：基于 DeepSeek-V3 官方定价（输入 ¥2/百万tokens，输出 ¥3/百万tokens）
- 国际化：更新中英文文案支持新功能
```

## Modification

### 修改文件列表

1. **client/src/views/Benchmark.vue**
   - 添加 `activeTab` 状态管理（qps/cost 两种模式）
   - 新增 Tab 切换 UI 组件（绿色 QPS 测试 / 紫色开销分析）
   - 根据 `activeTab` 动态显示控制栏参数：
     - QPS 模式：threads、connections、duration
     - 开销模式：logCount（日志总数）
   - 重构指标列表：
     - `qpsMetrics`: qps、p50、p99、total_logs
     - `costMetrics`: ai_calls、input_tokens、output_tokens、estimated_cost
   - 新增价格参考卡片（左下角，仅开销模式显示）
   - 优化 `finishBenchmark()` 逻辑：
     - QPS 测试：去除 net_io 和 scheduler 数据
     - 开销分析：基于批处理差异计算成本
       - Python：无批处理，每条日志单独调用
       - LogSentinel：批处理（batchSize=100），大幅降低 AI 调用次数
   - 更新标题：LogSentinel (C++ Reactor) → LogSentinel

2. **client/src/i18n.ts**
   - 新增 `benchmark.tabs`：qps/cost
   - 新增 `benchmark.totalLogs`、`benchmark.logs`
   - 更新 `benchmark.metrics`：
     - 移除：net_io、scheduler、total_ai
     - 新增：ai_calls、input_tokens、output_tokens、estimated_cost
   - 新增 `benchmark.priceRef`：title、model、inputPrice、outputPrice

## Learning Tips

### Newbie Tips

1. **Vue 3 响应式设计技巧**：
   - 使用 `computed` 属性根据 `activeTab` 动态返回指标列表，避免重复渲染
   - `v-if` 和 `v-for` 配合实现条件渲染，保持代码简洁

2. **国际化最佳实践**：
   - 所有用户可见文本都应使用 `$t()` 包裹，而非硬编码
   - 嵌套路径（如 `benchmark.metrics.qps`）便于组织复杂文案结构

3. **价格计算精度**：
   - 使用 `toFixed(2)` 保留两位小数，符合货币显示习惯
   - 注意 JavaScript 浮点数精度问题，金融计算建议使用整数运算后转换

### Function Explanation

1. **`computed` vs `ref`**：
   - `computed` 用于派生状态（如 `currentMetrics`），自动缓存结果，依赖不变不会重新计算
   - `ref` 用于原始响应式数据（如 `activeTab`、`pythonData`）

2. **`toLocaleString()` 格式化数字**：
   - 自动添加千位分隔符（如 10,000），提升可读性
   - 支持国际化，根据用户语言环境自动调整格式

3. **Element Plus Icon 使用**：
   - 需显式导入：`import { Loading, VideoPlay, InfoFilled } from '@element-plus/icons-vue'`
   - 使用 `<el-icon>` 组件包裹，通过 `is-loading` 类名实现旋转动画

### Pitfalls

1. **避免在模板中直接写复杂逻辑**：
   - ❌ 错误：`{{ pythonData.estimated_cost ? `¥${pythonData.estimated_cost}` : '--' }}`
   - ✅ 正确：使用计算属性或方法封装，保持模板简洁

2. **定时器清理**：
   - `setInterval` 返回值需要存储（`timer`），并在 `onUnmounted` 钩子中清理
   - 未清理定时器会导致内存泄漏，尤其在频繁切换组件时

3. **类型断言谨慎使用**：
   - 代码中使用了 `// @ts-ignore` 忽略 setInterval 类型检查
   - 更好的做法是正确定义类型：`declare function setInterval(handler: () => void, timeout: number): number`

4. **硬编码魔法数字**：
   - 当前代码中批处理大小（100）、token 估算值（150、50、3000、800）都是硬编码
   - 建议提取为常量或配置项，便于后续调整和维护

5. **价格计算假设**：
   - 当前假设所有场景都是缓存未命中（¥2/百万tokens 输入）
   - 实际生产中应考虑缓存命中率，使用加权平均价格更准确

## 技术亮点

- **批处理优势可视化**：通过对比 Python 和 LogSentinel 的开销，直观展示批处理架构的价值（约节省 99% AI 调用次数和 81% 成本）
- **价格透明化**：左下角价格参考卡片让用户清楚计费标准，提升信任度
- **动态 UI 架构**：使用 `computed` + `v-if` 实现单页面多模式，避免组件拆分过度

## 后续优化建议

1. **接入真实 API**：
   - QPS 测试：调用后端 `/benchmark` 接口执行 wrk 命令
   - 开销分析：基于历史数据统计真实 token 消耗

2. **价格配置化**：
   - 支持切换 AI Provider（Gemini、DeepSeek 等）
   - 从配置中心读取最新价格，自动更新计算逻辑

3. **导出报告**：
   - 支持导出测试结果为 JSON/PDF
   - 生成性能对比图表（ECharts 集成）
