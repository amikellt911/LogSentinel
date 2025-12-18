
## 2025-12-18
- **Refactoring**: 将日志风险等级从 High/Medium/Low 重构为 Critical/Error/Warning/Info/Safe。
- **Feature**: 引入 C++ `RiskLevel` 枚举类，并支持旧数据（Legacy Data）的兼容性读取（High -> Critical 等）。
- **Dashboard**: 更新仪表盘统计逻辑，显式计算 Safe 级别的日志数量，不再将其归类为未知。
- **Frontend**: 更新前端 API 接口定义和 Mock 数据生成逻辑，适配新的风险等级命名规范。

## 2025-12-18
- **Refactoring**: 将日志风险等级从 High/Medium/Low 重构为 Critical/Error/Warning/Info/Safe。
- **Feature**: 引入 C++ `RiskLevel` 枚举类，并支持旧数据（Legacy Data）的兼容性读取（High -> Critical 等）。
- **Dashboard**: 更新仪表盘统计逻辑，显式计算 Safe 级别的日志数量，不再将其归类为未知。
- **Frontend**: 更新前端 API 接口定义和 Mock 数据生成逻辑，适配新的风险等级命名规范。
