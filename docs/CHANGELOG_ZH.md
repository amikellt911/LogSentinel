# 更新日志 (Changelog)

## 2025-12-18 更新：配置持久化与活跃Prompt功能

### 修改内容
1.  **后端 (`server/`)**
    *   在 `AppConfig` 结构体中新增了日志策略、网络配置、高可用配置等相关字段。
    *   在 `SqliteConfigRepository` 中实现了这些新字段的数据库读写逻辑。
    *   新增 `active_prompt_id` 字段，用于明确指示当前激活的 Prompt。
    *   更新了数据库初始化脚本，新增了默认配置项。

2.  **前端 (`client/`)**
    *   更新了 `AISettings` 接口和 `system` store，支持 `activePromptId`。
    *   Settings 页面支持保存新增的配置项（日志保留天数、端口、熔断策略等）。
    *   Prompt 列表的点亮逻辑改为基于 `activePromptId` 判断，解决了之前的逻辑模糊问题。

### 新手避坑指南 (Tips for Newbies)
1.  **JSON 序列化宏 (`NLOHMANN_DEFINE_TYPE_INTRUSIVE`)**
    *   当你给 C++ 结构体加字段时，千万别忘了同步修改这个宏！
    *   如果你忘了，前端传过来的 JSON 数据虽然看着没报错，但后端死活解析不进结构体，你会查到怀疑人生。
    *   *经验之谈*：只要动了 `struct`，第一反应就是去找下面有没有 `NLOHMANN_...` 宏。

2.  **SQLite 的 `INSERT OR IGNORE`**
    *   我们在 `init_sql` 里用了这个语句来插入默认配置。
    *   这意味着如果 `config_key` 已经存在，它就不会覆盖。
    *   如果你修改了默认值想生效，开发环境下最快的方法是删掉数据库文件（通常在 `server/persistence/data/` 下），让它重新生成。

3.  **前端类型转换**
    *   后端传过来的 `config` map 全是字符串 (`Record<string, string>`)。
    *   在 `system.ts` 的 `fetchSettings` 里，记得用 `parseInt` 或 `=== '1'` 把它们转回正确的 number 或 boolean 类型。
    *   如果你直接赋值，Vue 的组件（比如 Switch 或 InputNumber）可能会报错或者表现怪异。

4.  **Active Prompt 的逻辑**
    *   之前是靠遍历 `prompts` 数组设置 `is_active` 字段，容易出现“多选”或“无选”的脏数据。
    *   现在改为一个全局的 `active_prompt_id`，单一信源（Source of Truth），逻辑更清晰。

### 常用命令备忘
*   **编译后端**: `cd server && mkdir -p build && cd build && cmake .. && make -j4`
*   **编译前端**: `cd client && npm run build`
