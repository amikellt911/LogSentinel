# 项目更新文档 - 国际化配置持久化

## 1. 修改概述
本次更新主要实现了界面语言（Language）配置的后端持久化，并调整了前端设置入口。现在用户选择的语言（中文/英文）会保存在数据库中，重启后依然生效。

### 主要变更点
1.  **后端 (C++)**:
    *   在 `AppConfig` 结构体中新增 `app_language` 字段。
    *   数据库 `app_config` 表初始化时增加 `app_language` 默认值 (`en`)。
    *   `SqliteConfigRepository` 支持对 `app_language` 的读写。
2.  **前端 (Vue/TS)**:
    *   移除顶部导航栏右上角的语言切换按钮。
    *   在 **设置 (Settings)** 页面新增 **"General" (常规)** 选项卡。
    *   Pinia `system` store 新增 `general.language` 状态，并监听变化自动切换 `i18n` 语言。
    *   设置保存时会将语言偏好同步提交到后端。

---

## 2. 详细实现说明 (给新手的 Tips)

### 2.1 后端部分
**文件**: `server/persistence/ConfigTypes.h`
*   **Struct 序列化宏**:
    我使用了 `NLOHMANN_DEFINE_TYPE_INTRUSIVE` 宏。
    ```cpp
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AppConfig, ..., app_language, ...)
    ```
    *Tip*: 这个宏非常方便，它自动为你的结构体生成 `to_json` 和 `from_json` 函数。**但是**，你必须确保宏里面的字段顺序、名称和 JSON key 完全一致，否则反序列化时可能会对不上或者报错。

**文件**: `server/persistence/SqliteConfigRepository.cpp`
*   **手动解析 KV**:
    虽然我们在 `ConfigTypes.h` 里用了 JSON 宏，但在从 SQLite 的 `app_config` 表（Key-Value 结构）读取时，我们还是用了一堆 `if-else` 来判断 key。
    ```cpp
    else if (key == "app_language") config.app_language = val;
    ```
    *Tip*: 这里看起来有点笨（if-else），但在 C++ 中对于这种少量固定的配置项，`if-else` 配合 `std::string` 的性能完全足够，而且比引入反射机制或复杂的 map 映射更直观、易调试。不要为了“优雅”而过度设计。

### 2.2 前端部分
**文件**: `client/src/stores/system.ts`
*   **Vue Watch**:
    我在 Pinia store 里使用了 `watch`。
    ```typescript
    watch(() => settings.general.language, (newLang) => {
        i18n.global.locale.value = newLang
    })
    ```
    *Tip*: 这是因为 `settings` 对象是响应式的，但 `i18n` 的 locale 也是响应式的。我们需要一个桥梁，当用户在 UI 修改 `settings` 时，立即生效（不用等保存）。这种“副作用”逻辑放在 store 的 setup 函数里是很合适的。

**文件**: `client/src/views/Settings.vue`
*   **Element Plus Tabs**:
    我增加了一个新的 `<el-tab-pane>`。
    ```vue
    <el-tab-pane :label="$t('settings.tabs.general')">
    ```
    *Tip*: 注意 `:label` 用了 `$t(...)`，这是为了让“General”这个标签本身也能随着语言切换而变（变成“常规”）。如果你写死成字符串 "General"，那切到中文时它还是英文。

---

## 3. 踩坑记录 (避坑指南)

1.  **Element Plus Icon 引入**:
    在 `Settings.vue` 中，我引入了 `<Setting />` 图标用于新的 Tab。
    *坑*: 刚开始只引入了没在 `components` 注册或者没在 script setup 显式使用（只是放在 template 里），有些构建配置可能会报 "unused import" 或者类型错误。
    *解*: 确保引入并在模板正确使用。如果是动态组件 `<component :is="...">`，要确保变量在 scope 内。在这个项目中，直接 `<el-icon><Setting /></el-icon>` 配合 `import { Setting } ...` 是最稳妥的。

2.  **Playwright 测试网络问题**:
    在写验证脚本时，试图直接访问 `http://localhost:4173/settings`。
    *坑*: 在某些 CI/容器环境中，`localhost` 可能解析不到，或者 Vite preview 还没完全启动脚本就跑了。
    *解*: 脚本里我加了 `.wait_for_load_state("networkidle")`。如果还不行，通常要在 bash 里先确保 `npm run preview` 已经在后台跑起来且端口正确。
    *Tip*: 如果前端页面加载不出来，Playwright 会报超时。这时候先用 `curl -v` 看看端口通不通。

3.  **TS 类型定义**:
    修改 `system.ts` 时，我加了 `GeneralSettings` 接口。
    *坑*: 如果只在 `state` 里加了数据，没在 `interface` 定义里加，TypeScript 可能会报错或者没有补全。
    *解*: 养成好习惯，改 state 结构前，先改 interface。

---

希望这份文档能帮你更好地理解这次修改！
