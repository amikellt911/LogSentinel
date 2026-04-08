# Git Commit Message

feat(frontend): 新增设置页信息架构原型

# Modification

- `client/src/views/SettingsPrototype.vue`
- `client/src/router/index.ts`
- `client/src/layout/MainLayout.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

# 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么这个页面故意不接旧 `systemStore`、为什么左侧只做信息架构导航、为什么 AI/告警采用双栏工作区，以及为什么内核页把高级参数下沉到折叠区。
- 在 `client/src/router/index.ts` 里补了中文注释，说明为什么新原型页先独立挂载到 `/settings-prototype`，而不是直接覆盖旧 `Settings.vue`。
- 在 `client/src/layout/MainLayout.vue` 里补了中文注释，说明为什么原型页虽然不放进正式侧边栏，但顶部标题仍然要准确反映当前页面。

# Learning Tips

## Newbie Tips

设置页原型第一步不该直接改旧表单，而该先验证信息架构。因为设置页的问题通常不是“少了几个输入框”，而是字段分组、层级和语义混在一起。先把“基础 / AI / 告警 / 内核”这四个域拆干净，后面再接真实保存逻辑才不会越改越乱。

## Function Explanation

这次的 `SettingsPrototype.vue` 不是功能页，而是结构原型页。它用本地 reactive 状态模拟字段，重点是验证：哪些东西该放一个 tab、哪些该做双栏、哪些高级参数应该折叠到底部，而不是先急着把旧 store 全部绑上来。

## Pitfalls

如果你直接在旧 `Settings.vue` 上边删边改，用户很容易在“旧字段还没删干净、新字段又没接真实逻辑”的中间态里迷路。原型页独立挂路由的价值就在这里：你可以并行对比新旧结构，不会把现有页面一下改死。

# Verification

- 运行了 `cd client && npm run build`
- 当前构建仍失败，但失败点集中在仓库已有旧文件：
  - `src/components/AIEngineSearchBar.vue`
  - `src/components/BatchArchiveList.vue`
  - `src/components/BusinessHealthCards.vue`
  - `src/components/PromptDebugger.vue`
  - `src/components/RiskDistribution.vue`
  - `src/components/TraceWaterfall.vue`
  - `src/stores/system.ts`
  - `src/views/AIEngine.vue`
  - `src/views/History.vue`
  - `src/views/Settings.vue`
- 本次新增/修改的 `SettingsPrototype.vue`、`router/index.ts`、`layout/MainLayout.vue` 未出现在构建报错列表中。

# 追加记录：2026-04-08 设置原型页补挂侧边栏入口

## Git Commit Message

fix(frontend): 补挂设置原型页侧边栏入口

## Modification

- `client/src/layout/MainLayout.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/layout/MainLayout.vue` 里补了中文注释，说明为什么 `settings-prototype` 需要先并行挂到左侧边栏，而不是只留独立路由让用户手输地址。

## Learning Tips

### Newbie Tips

页面“已经有路由”不等于“用户真的能看见”。如果用户的操作路径是靠左侧导航驱动的，那原型页不挂入口，实际效果就等于没交付。

### Function Explanation

这次没有改路由本身，只是在 `MainLayout` 侧边栏增加一个新的 `el-menu-item`，让 `/settings-prototype` 能和旧 `/settings` 并行存在，方便直接对比。

### Pitfalls

只在顶部标题里处理 `settings-prototype` 的名字，而不把它放进侧边栏，会导致开发者自己知道新页存在，用户却完全找不到入口。这种“实现存在但路径不可达”的问题，本质上还是 UI 集成没完成。

# 追加记录：2026-04-08 设置原型页交互语义收口

## Git Commit Message

refactor(frontend): 收口设置原型页交互语义

## Modification

- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么主设置域仍维持 4 个 tab、为什么脏状态和重启项要单独计算、以及为什么“发送测试消息”必须和“保存原型快照”拆开。

## Learning Tips

### Newbie Tips

原型页不是把几个输入框摆上去就完了。只要页面上同时出现“保存配置”和“测试动作”，你就必须把这两个动作的语义拆开。否则用户点了“测试发送”，结果把整页配置顺手保存了，这就是典型的交互污染。

### Function Explanation

这次新增的 `dirtySections` 是把当前本地状态和上一次保存快照逐段比对，告诉用户到底是哪一类配置发生了变化。`restartRequired` 则只关心冷启动字段有没有被改动，用来在保存栏里提前提示“保存后需重启”。

### Pitfalls

`savePrototype()` 如果先覆盖 `savedSnapshot`，再去判断哪些字段需要重启，就会永远判断不出来，因为“当前状态”和“已保存状态”已经被你写成一样了。这个顺序错了，提示就一定失真。

## Verification

- 运行了 `cd client && npm run build`
- 当前构建仍失败，但失败点仍集中在仓库已有旧文件：
  - `src/components/AIEngineSearchBar.vue`
  - `src/components/BatchArchiveList.vue`
  - `src/components/BusinessHealthCards.vue`
  - `src/components/PromptDebugger.vue`
  - `src/components/RiskDistribution.vue`
  - `src/components/TraceWaterfall.vue`
  - `src/stores/system.ts`
  - `src/views/AIEngine.vue`
  - `src/views/History.vue`
  - `src/views/Settings.vue`

# 追加记录：2026-04-08 AI Prompt 编辑区解除高度锁定

## Git Commit Message

refactor(frontend): 放大设置原型页的 prompt 编辑工作区

## Modification

- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么 AI tab 要去掉写死高度，改成页面自然滚动，而不是继续把 Prompt 工作区锁在固定盒子里。
- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么右侧改成“名称区 + 正文主编辑区”的单一工作面板，不再把正文继续塞进 `el-form-item` 的默认包围层。
- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么保存区要提升成页面级底栏，以及为什么 textarea 的三层 Element Plus 壳子都要一起继承高度。

## Learning Tips

### Newbie Tips

编辑器看起来小，很多时候不是 textarea 自己太矮，而是外层容器先被你写死了。既然上面配置区和下面保存区都要保留，那么正确做法通常不是继续压紧别的区域，而是先把“谁跟谁共用一个高度盒子”这件事拆开。

### Function Explanation

这次把页面改成“内容滚动区 + 页面级底栏”两层。这样 AI tab 里的 Prompt 面板可以按内容自然长高，保存栏仍然固定属于整个 Settings 页面，但不再属于 Prompt 编辑工作区本身。

### Pitfalls

只把固定高度从父容器上删掉还不够。如果你还让正文 textarea 留在 `el-form-item` 的默认内容壳子里，或者只给最内层 `.el-textarea__inner` 写 `height: 100%`，用户最后看到的输入区还是会像缩在一个小框里。

## Verification

- 运行了 `cd client && npm run build`
- 当前构建仍失败，但失败点仍集中在仓库已有旧文件：
  - `src/components/AIEngineSearchBar.vue`
  - `src/components/BatchArchiveList.vue`
  - `src/components/BusinessHealthCards.vue`
  - `src/components/PromptDebugger.vue`
  - `src/components/RiskDistribution.vue`
  - `src/components/TraceWaterfall.vue`
  - `src/stores/system.ts`
  - `src/views/AIEngine.vue`
  - `src/views/History.vue`
  - `src/views/Settings.vue`
- 本次修改的 `src/views/SettingsPrototype.vue` 未出现在构建报错列表中。
- 运行了单文件 SFC 编译检查：
  - `node -e "...@vue/compiler-sfc..."` 返回 `SFC compile ok`
- 本次修改的 `src/views/SettingsPrototype.vue` 未出现在报错列表中。

# 追加记录：2026-04-08 设置原型页修复 runtime-only 渲染问题

## Git Commit Message

fix(frontend): 修复设置原型页局部组件无法渲染

## Modification

- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么这些辅助组件不能继续写成 `template:` 字符串，而必须改成 render 函数，否则当前 runtime-only Vue 构建下页面只会剩空壳。

## Learning Tips

### Newbie Tips

Vue 里“组件注册成功”不等于“组件能渲染”。如果你的工程用的是 runtime-only 构建，那么组件选项里的字符串模板不会在浏览器里现编译，结果就是外层页面在，里面的局部组件全空。

### Function Explanation

这次把 `StatusChip`、`FieldBox`、`SidePanel`、`SideRow`、`SwitchBlock`、`WatermarkItem` 都改成了 render 函数。这样模板编译工作在构建阶段就完成了，不再依赖运行时编译器。

### Pitfalls

这个问题最坑的地方在于它不是直接编译报错，而是开发时给一个 Vue warn，然后页面还能半开半不开地显示标题和容器，容易误判成“原型设计有问题”，实际先死的是渲染方式。

## Verification

- 用本地 `@vue/compiler-sfc` 对 `client/src/views/SettingsPrototype.vue` 做了单文件解析和模板编译检查，结果通过：`SFC compile ok`

# 追加记录：2026-04-08 Settings 存储契约第一刀收口

## Git Commit Message

refactor(persistence): 收口 Settings 的持久化字段契约

## Modification

- `server/persistence/ConfigTypes.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `server/persistence/ConfigTypes.h` 的 `AppConfig` 上方补了中文注释，说明为什么 `app_config` 继续保留 KV 投影，而不是现在就拆成强 schema 配置表。
- 在 `server/persistence/ConfigTypes.h` 的 `PromptConfig` 上方补了中文注释，说明为什么 prompt 存储这轮故意保持最小字段集合，不把还没钉死的语义提前写进表结构。
- 在 `server/persistence/ConfigTypes.h` 的 `AlertChannel` 上方补了中文注释，说明为什么渠道存储去掉 `msg_template`、改成保留 `secret`。
- 在 `server/persistence/SqliteConfigRepository.cpp` 的布尔解析 helper、初始化 SQL 和 `handleUpdateAppConfig()` 上方补了中文注释，说明为什么要做宽容布尔解析、为什么这轮只收口存储契约、以及为什么 `config` 写入改成按 key upsert。

## Learning Tips

### Newbie Tips

配置存储最容易犯的错，不是字段少几个，而是把“表结构”和“配置语义”绑死。像 `app_config` 这种明显还会继续增删 key 的区域，先维持 KV，比现在就拆一张超宽大表更稳。

### Function Explanation

这次 `SqliteConfigRepository` 里的 `handleUpdateAppConfig()` 从单纯 `UPDATE` 改成了按 key `UPSERT`。这样前端以后按 tab 只发改动字段时，哪怕某个 key 之前没 seed 进去，也不会因为 update 命中 0 行而悄悄丢掉这次配置。

### Pitfalls

`alert_channels` 这次只是把新 schema 和 Repository 读写口径收口成 `secret` 版，没有在这一步补旧库迁移逻辑。所以如果你拿的是旧测试库，就要准备删库重建；否则表里还停留在 `msg_template` 老结构，后面联调时一定会撞字段不匹配。

## Verification

- 按本轮要求，未运行编译命令和测试命令。
- 这一步只做了存储契约收口，没有继续向前端、Handler 和运行时消费点扩散。

# 追加记录：2026-04-08 ConfigHandler 收口 Settings 接口层口径

## Git Commit Message

refactor(handler): 收口 Settings 接口层字段白名单

## Modification

- `server/handlers/ConfigHandler.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

# 追加记录：2026-04-08 Settings 原型页改成结构化 Prompt 编辑器

## Git Commit Message

refactor(frontend): 将设置原型页的业务 prompt 改成结构化编辑

## Modification

- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 的 AI tab 右侧编辑区附近补了中文注释，说明为什么业务 Prompt 不再给一个自由大文本框，而要按单值、术语字典、多条规则三种数据形态拆开。
- 在 `client/src/views/SettingsPrototype.vue` 的 `parsePromptContent()` 上方补了中文注释，说明为什么当前原型页回填时要优先按 JSON 结构解析，以及为什么旧自由文本要临时兜底到 `domainGoal`，避免原内容直接丢失。
- 在 `client/src/views/SettingsPrototype.vue` 的 `renderBusinessGuidancePreview()` 上方补了中文注释，说明为什么结构化编辑器旁边还要保留只读预览。
- 在 `client/src/views/SettingsPrototype.vue` 的 `persistSettings()` 上方旧注释附近继续沿用并匹配当前改动，保证“原型页验证新字段契约”的语义不被旧自由文本编辑逻辑误导。

## Learning Tips

### Newbie Tips

如果你嘴上说“业务 Prompt 只能补领域知识”，但界面上还是给用户一个无限自由的大文本框，那这个限制就是假的。真正的边界不是写在文档里，而是应该长在编辑器结构里。

### Function Explanation

这次新增的 `PromptContentDraft` 不是最终发给模型的字符串，而是前端编辑态的数据结构。真正给模型看的 `business_guidance` 仍然是通过 `renderBusinessGuidancePreview()` 这种渲染逻辑拼出来的。也就是说，编辑态和运行态现在被明确分开了。

### Pitfalls

如果你直接把旧库里的 `content` 当成 JSON 强行解析，一旦里面还是自由文本，原型页就会直接炸掉或者清空内容。这次选择的兜底方式是先把旧文本塞进 `domainGoal`，这样虽然语义不完美，但至少不会让历史内容无声丢失。

## Verification

- 在 `client` 目录运行了单文件 SFC 编译检查：
  - `node -e "...@vue/compiler-sfc..."` 返回 `SFC compile ok`
- 第一次在仓库根目录运行相同检查时失败，原因是 `@vue/compiler-sfc` 模块只安装在 `client/node_modules`，不是本次代码本身的语法错误。

- 在 `server/handlers/ConfigHandler.cpp` 顶部 helper 区补了中文注释，说明为什么接口层现在必须自己维护 config key 白名单，以及为什么 channels 要先硬收成飞书最小字段集。
- 在 `server/handlers/ConfigHandler.cpp` 的 `ConvertConfigValueToString()` 上方补了中文注释，说明为什么 `/settings/config` 这条接口现在只允许标量值，不再允许对象/数组混进 KV 配置。
- 在 `server/handlers/ConfigHandler.cpp` 的 `handleUpdateAppConfig()` 和 `handleUpdateChannels()` 里补了中文注释，说明为什么这一步要在 Handler 层先把旧字段挡掉，而不是继续依赖 Repository 默默忽略。

## Learning Tips

### Newbie Tips

如果存储层已经收口成新契约，接口层却还继续吃旧字段，那“代码能跑”不代表系统口径一致。最典型的坑就是数据库里继续被写进没人再读的废 key，后面你看页面、看库、看运行时，三边都对不上。

### Function Explanation

这次 `ConfigHandler` 里新加的 helper 做的是两层过滤：`/settings/config` 先验 key 白名单，再把标量值统一转成字符串；`/settings/channels` 则先验字段集合和 provider，只让飞书最小字段集通过。

### Pitfalls

这一步会让旧前端保存直接暴露 400，因为 `system.ts` 现在还在发 `msg_template`、`kernel_io_buffer`、`kernel_refresh_interval` 这些已经被收掉的字段。这不是新 bug，而是接口层开始真实反映前后端口径已经不一致，下一刀就该改前端映射。

## Verification

- 按本轮要求，未运行编译命令和测试命令。
- 这一步只修改了 `ConfigHandler` 接口层口径，没有继续改前端 store、Repository 或主程序运行时读取。

# 追加记录：2026-04-08 回收 ConfigHandler 里的重复白名单

## Git Commit Message

refactor(handler): 回收设置接口层的重复字段白名单

## Modification

- `server/handlers/ConfigHandler.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `server/handlers/ConfigHandler.cpp` 的 `handleUpdateAppConfig()` 上方补了中文注释，说明为什么这里退回到最小职责，只做请求形状和标量值校验，不再自己维护第二份字段白名单。
- 在 `server/handlers/ConfigHandler.cpp` 的 `handleUpdateChannels()` 上方补了中文注释，说明为什么 channels 也只保留最小格式校验，不再在 Handler 层重复维护飞书字段契约。
- `ConvertConfigValueToString()` 上方原有中文注释保留，继续说明为什么 `/settings/config` 只接受标量值。

## Learning Tips

### Newbie Tips

“多一层校验更安全”不总是对的。只要这层校验和真正的数据契约不是同一个来源，它就会变成第二份配置表，后面字段一改，你就要同时维护两边，出错概率反而更高。

### Function Explanation

这次不是把格式校验全删掉，而是把 `ConfigHandler` 收回到最小职责：`/settings/config` 仍然要求 `items` 数组和标量 `value`，`/settings/channels` 仍然要求数组和基本必填字段，只是不再在这一层硬编码完整字段集合和 provider 白名单。

### Pitfalls

如果前端和存储层都还没稳定，就急着在 Handler 里写死一整份 key 列表，短期看像是更严谨，实际上后面每改一个字段都要追着这里一起动。等 Settings 真定稿、而且你确实需要接口层强校验时，再把这层补回来更合适。

## Verification

- 按本轮要求，未运行编译命令和测试命令。
- 这一步只回收了 `ConfigHandler` 里过重的字段白名单，没有继续改前端 store、Repository 或主程序运行时读取。

# 追加记录：2026-04-08 设置原型页接回真实存储闭环

## Git Commit Message

feat(settings): 打通设置原型页与配置存储闭环

## Modification

- `client/src/views/SettingsPrototype.vue`
- `client/src/main.ts`
- `client/src/i18n.ts`
- `server/persistence/ConfigTypes.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么这页故意不复用旧 `system.ts`、为什么 `trace_end_aliases` 先按 JSON 字符串存储，以及为什么本地快照现在仍然要保留给 dirty check / restart 判断使用。
- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么“测试发送”和“保存设置”必须分开，避免把渠道联调动作和整页配置保存混成一个语义。
- 在 `client/src/main.ts` 里补了中文注释，说明为什么应用入口要先预取 `app_language`，否则根路由第一次打开时会先展示错误语言。
- 在 `client/src/i18n.ts` 里补了中文注释，说明为什么默认语言先切到中文，以及为什么最终语言仍然要由入口预取的配置覆盖。
- 在 `server/persistence/ConfigTypes.h` 和 `server/persistence/SqliteConfigRepository.cpp` 里补了中文注释，说明为什么 `trace_end_aliases` 当前先按 JSON 字符串挂进 `app_config`，而不是现在就把 KV 存储改成复杂结构。

## Learning Tips

### Newbie Tips

设置页“能保存”不等于“真闭环”。真正的闭环至少要有三步：页面加载能回填、保存后刷新还能读回来、入口级全局状态能在用户第一次打开应用时就拿到。少任何一步，用户都会觉得配置像是假的。

### Function Explanation

这次 `SettingsPrototype.vue` 没去复用旧 `system.ts`，而是直接按三类接口对接：`/settings/config`、`/settings/prompts`、`/settings/channels`。这样做是为了绕开旧 store 里已经过时的字段映射，先验证新字段契约本身能不能稳定落库和回填。

### Pitfalls

`trace_end_aliases` 现在只是“存得下、读得回”，还没有真正接进 `LogHandler` 的解析逻辑。所以这一步解决的是设置页假保存，不是别名已经开始生效。后面如果你看到界面能保存别名，但上报时别名还不认，这是预期中的下一阶段工作。

## Verification

- 按本轮要求，未运行编译命令和测试命令。
- 这一步只打通了“设置原型页 <-> 配置存储 <-> 应用入口语言预取”的闭环，没有继续改主程序运行时消费点。

# 追加记录：2026-04-08 结束字段别名接入 LogHandler 解析入口

## Git Commit Message

feat(trace): 让结束字段别名在解析入口真实生效

## Modification

- `server/handlers/LogHandler.h`
- `server/handlers/LogHandler.cpp`
- `server/src/main.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `server/handlers/LogHandler.h` 的构造函数注释里补了中文说明，解释为什么这次要给 `LogHandler` 注入 `ConfigRepo`，以及为什么结束字段配置必须按“单请求拿一次快照”读取，不能在构造时缓存死。
- 在 `server/handlers/LogHandler.cpp` 里给 `ParseTraceEndAliasList()` 补了中文注释，说明为什么别名 JSON 解析失败时要宽容降级成“无别名”，而不是直接打断 span 上报。
- 在 `server/handlers/LogHandler.cpp` 里给 `ParseConfiguredTraceEnd()` 补了中文注释，说明当前采用“主字段优先、别名顺序回退”的匹配规则，避免同一条请求里多个字段互相覆盖。
- 在 `server/handlers/LogHandler.cpp` 的 `CollectUnknownTopLevelAttributes()` 和 `handleTracePost()` 附近补了中文注释，说明为什么结束字段主名/别名要从未知顶层属性收集中排除，以及为什么这一刀要在请求开头拿一份配置快照后整段复用。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么要把 `config_repo` 传给 `LogHandler`，以及这一步只服务结束字段标准化，不代表 Handler 开始承担通用配置读取职责。

## Learning Tips

### Newbie Tips

热配置最容易写脏的地方，不是“怎么拿到最新值”，而是“同一条请求内部会不会前半段用旧值、后半段用新值”。所以这类解析配置不能随用随读，应该在请求入口拿一次快照，然后整段都用同一份。

### Function Explanation

这次没有改 `TraceSessionManager`，而是把 `trace_end_field / trace_end_aliases` 的处理放在 `LogHandler`。原因是别名本质属于“协议解析层”的标准化：请求里可能叫 `trace_end`、`end` 或其他别名，但进入主链后统一只认 `span.trace_end`。

### Pitfalls

如果把 `trace_end` 永久写死在“已知顶层字段”列表里，那么当用户把主字段改成别的名字后，旧的 `trace_end` 既不会被识别成结束标记，也不会落进 `attributes`，会被静默吞掉。这一刀把结束字段移到动态已知字段里，就是为了避免这种假成功。

## Verification

- 按本轮要求，未运行编译命令和测试命令。
- 这一步只把 `trace_end` 主字段/别名接入 `LogHandler` 解析入口，没有继续改 `TraceSessionManager`、其他运行时消费点，也没有动 `ConfigRepo` 的 mutex/atomic 实现。

# 追加记录：2026-04-08 结束字段别名拆出单独表并移出热路径

## Git Commit Message

refactor(config): 将结束字段别名改为单独表与快照数组

## Modification

- `server/persistence/ConfigTypes.h`
- `server/persistence/SqliteConfigRepository.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `server/handlers/LogHandler.cpp`
- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `server/persistence/ConfigTypes.h` 里补了中文注释，说明为什么 `trace_end_aliases` 要在内存快照里直接改成 `vector<string>`，以及为什么不能继续让热路径反复反序列化字符串。
- 在 `server/persistence/SqliteConfigRepository.h` 里补了中文注释，说明为什么要新增 `getTraceEndAliasesInternal()`，以及这张别名表的职责就是把控制面存储和热路径读取隔开。
- 在 `server/persistence/SqliteConfigRepository.cpp` 里补了中文注释，说明为什么别名字符串只允许在 Repository 写入边界解析一次、为什么要用单独的 `trace_end_aliases` 表保住顺序、以及为什么主字段变化时要重新规整别名列表。
- 在 `server/handlers/LogHandler.cpp` 里补了中文注释，说明为什么解析层现在直接读取快照数组，不再自己做 JSON 解析。
- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么前端保存时暂时还要 `JSON.stringify`，以及这一步只是为了兼容当前 `/settings/config` 标量接口，而不代表后端仍然按 JSON 字符串持久化。

## Learning Tips

### Newbie Tips

热路径优化最常见的误区，就是只盯着“这一行代码是不是快”，却不看“这件事是不是根本就不该出现在这里”。这次真正该删掉的不是某个 JSON 库调用，而是“每个请求都做别名反序列化”这个职责本身。

### Function Explanation

这次 `trace_end_aliases` 变成了“SQLite 单独表 + `AppConfig` 内存数组”的两层模型。磁盘层负责持久化和顺序，Repository 负责把存储值装配成快照对象，`LogHandler` 只消费已经准备好的 `vector<string>`。

### Pitfalls

如果你只是把 `AppConfig.trace_end_aliases` 改成 `vector<string>`，但底层还是继续往 `app_config` 里塞 JSON 字符串、并且在 `LogHandler` 里每请求 `parse`，那只是把类型写好看了，热点开销根本没消失。真正要收掉的是“解析发生的时机”和“别名落在哪里”。

## Verification

- 按本轮要求，未运行编译命令和测试命令。
- 这一步只重构了 `trace_end_aliases` 的持久化与快照模型，没有继续扩到 `ConfigHandler` 数组接口、`TraceSessionManager` 其他运行时参数消费，也没有改 `ConfigRepo` 的 mutex/atomic 发布方式。

# 追加记录：2026-04-08 将结束字段别名去重责任收回前端

## Git Commit Message

refactor(settings): 将结束字段别名去重收回前端状态

## Modification

- `client/src/views/SettingsPrototype.vue`
- `server/persistence/SqliteConfigRepository.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 的 `normalizeAliasDrafts()` 上方补了中文注释，说明为什么重复别名、空值和与主字段撞名的项要在前端控件状态里立即清掉，而不是继续交给后端兜底。
- 在 `client/src/views/SettingsPrototype.vue` 的 `applySnapshot()` 附近补了中文注释，说明为什么回填后的别名数组也要再过一次前端规整，避免旧值或半迁移值把页面状态搞脏。
- 在 `server/persistence/SqliteConfigRepository.cpp` 的 `FilterTraceEndAliases()` 和 `loadFromDbInternal()` 附近补了中文注释，说明为什么后端现在只保留最小主字段冲突过滤，不再负责去重。

## Learning Tips

### Newbie Tips

如果一个约束本来就是 UI 自己能稳定保证的，例如多选标签去重，那么优先在前端状态层收掉，代码会比“前后端双重兜底”更短、更容易看懂。后端只应该保留那种真的会破坏语义的最低限度校验。

### Function Explanation

这次新增的 `normalizeAliasDrafts()` 不是给后端用的，而是给 `el-select multiple + allow-create` 这种可自由输入的控件做状态清洗。它会把别名 trim 后去重，并且把和主字段同名的项直接剔掉。

### Pitfalls

如果你只在“保存前”做一次别名规整，而不是在前端状态变化时立刻收掉，那么用户界面上仍然会暂时出现重复标签。后端虽然能处理，但用户会以为这些配置真的会原样保存，这就是状态展示和最终语义不一致。

## Verification

- 按本轮要求，未运行编译命令和测试命令。
- 这一步只把重复别名处理从后端收回前端，没有继续改 Settings 接口结构，也没有动 `LogHandler` 的结束字段匹配逻辑。

# 追加记录：2026-04-08 设置原型页去掉说明性文案

## Git Commit Message

refactor(frontend): 删除设置原型页说明性文案

## Modification

- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 头部补了中文注释，说明为什么这一版原型页要把说明性文案整体拿掉，只保留标题、字段标签和操作本身。
- 在 `client/src/views/SettingsPrototype.vue` 底部保存栏补了中文注释，说明为什么这里收口成纯动作区，不再显示状态说明文字。

## Learning Tips

### Newbie Tips

页面里最容易把主次关系做坏的，不是控件本身，而是那些“看起来好像很贴心”的辅助文字。只要这些灰字既不承载关键状态、又会挤占主编辑区，它们就应该先删，不要心软。

### Function Explanation

这次没有改字段语义，也没有改保存交互本身，只是把页面里的说明文案、提示块和底栏状态文案全部拿掉，让 Settings 原型页更像操作面板，而不是带大量注释的演示稿。

### Pitfalls

删说明文字时，别只盯着一两个 `text-xs`。真正影响观感的还有整块说明卡片、编辑区右侧的介绍面板，以及底栏里那种“保存状态 + 最近时间”的长句；这些不一起删，页面还是会显得啰嗦。

## Verification

- 运行了 `cd client && npm run build`
- 当前构建仍失败，但失败点仍集中在仓库已有旧文件：
  - `src/components/AIEngineSearchBar.vue`
  - `src/components/BatchArchiveList.vue`
  - `src/components/BusinessHealthCards.vue`
  - `src/components/PromptDebugger.vue`
  - `src/components/RiskDistribution.vue`
  - `src/components/TraceWaterfall.vue`
  - `src/stores/system.ts`
  - `src/views/AIEngine.vue`
  - `src/views/History.vue`
  - `src/views/Settings.vue`
- 本次修改的 `src/views/SettingsPrototype.vue` 未出现在构建报错列表中。
- 运行了单文件 SFC 编译检查：
  - `node -e "...@vue/compiler-sfc..."` 返回 `SFC compile ok`

# 追加记录：2026-04-08 Prompt 编辑区去掉无效字段

## Git Commit Message

fix(frontend): 精简设置原型页的 prompt 编辑区

## Modification

- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么 prompt 编辑区要删掉“业务作用域”，恢复成旧设置页那种“名称 + 单一主编辑器”的结构。

## Learning Tips

### Newbie Tips

编辑区里最值钱的是可连续输入的空间。只要一个字段既不稳定、又会挤占主输入区，你就应该先删它，而不是幻想用户会喜欢多一个输入框。

### Function Explanation

这次把 `PromptDraft.scope` 彻底移掉，右侧编辑区只保留 `name` 和 `content`。同时给 textarea 根节点和 inner 都补了高度与滚动兜底，确保它就是一整块可持续编辑的大输入区。

### Pitfalls

只给 textarea inner 写 `height: 100%` 还不够，如果外层 `.el-textarea` 本身没撑满，用户看到的仍然会像一块缩着的输入框，不像真正的编辑器。

## Verification

- 用本地 `@vue/compiler-sfc` 对 `client/src/views/SettingsPrototype.vue` 做了单文件解析和模板编译检查，结果通过：`SFC compile ok`
- 搜索确认 `SettingsPrototype.vue` 里已经没有 `scope` 或“业务作用域”字段残留
- 同时确认文件里已经没有局部组件的 `template:` 选项残留；搜索命中只剩注释说明文本

# 追加记录：2026-04-08 设置原型页按旧模板重排

## Git Commit Message

refactor(frontend): 按旧设置页模板重排设置原型

## Modification

- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么这页要直接复用旧 `Settings.vue` 的布局骨架，而不是继续发明新的工作台结构。
- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么保存区还要保留脏状态与重启字段提示，以及为什么“测试发送”和“保存原型快照”必须继续分开。

## Learning Tips

### Newbie Tips

做原型页时，用户如果明确说“按现有页那个样子来”，就别再自作聪明去换视觉体系。因为这时候用户要验证的不是你的审美，而是“新字段塞进旧产品骨架后，看起来是不是一套东西”。

### Function Explanation

这次把原型页重新收敛成旧 `Settings.vue` 的 `Header + border-card tabs + 深色卡片 + 主从编辑 + 底部保存区` 结构，只保留本地状态和已经钉死的新字段语义。

### Pitfalls

如果只是改颜色和几个 class，而不把整体骨架改回旧页的 `el-form + el-tabs + tab pane` 结构，用户看起来还是会觉得它是另一套页面。这个问题不是主题色，而是信息布局层级根本就不是一套系统。

## Verification

- 用本地 `@vue/compiler-sfc` 对 `client/src/views/SettingsPrototype.vue` 做了单文件解析和模板编译检查，结果通过：`SFC compile ok`
- 运行了 `cd client && npm run build`
- 当前构建仍失败，但失败点仍集中在仓库已有旧文件：
  - `src/components/AIEngineSearchBar.vue`
  - `src/components/BatchArchiveList.vue`
  - `src/components/BusinessHealthCards.vue`
  - `src/components/PromptDebugger.vue`
  - `src/components/RiskDistribution.vue`
  - `src/components/TraceWaterfall.vue`
  - `src/stores/system.ts`
  - `src/views/AIEngine.vue`
  - `src/views/History.vue`
  - `src/views/Settings.vue`
- 本次修改的 `src/views/SettingsPrototype.vue` 未出现在报错列表中。

# 追加记录：2026-04-08 Prompt 列表继续对齐旧设置页

## Git Commit Message

refactor(frontend): 对齐设置原型页的 prompt 列表交互

## Modification

- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `client/src/views/SettingsPrototype.vue` 里补了中文注释，说明为什么 prompt 列表要继续沿用旧 `Settings.vue` 的点击语义，也就是点击列表项时同时完成选中编辑和切换 active。

## Learning Tips

### Newbie Tips

列表页最容易做错的，不是颜色和边框，而是交互语义。旧页如果是“点一下就切 active”，你改成“先选中再点右侧按钮激活”，用户马上就会觉得这不是同一套产品。

### Function Explanation

这次把 prompt 列表项收敛回旧设置页那种行为：左边只显示名字，点击列表项时同步更新 `selectedPromptId` 和 `ai.activePromptId`，状态点和右侧编辑对象一起切。

### Pitfalls

如果左侧显示的是当前选中项，绿色状态点显示的是另一个 active 项，用户会立刻分不清“我现在在改哪条”和“系统实际在用哪条”。这种双状态分裂比样式丑更糟。

## Verification

- 用本地 `@vue/compiler-sfc` 对 `client/src/views/SettingsPrototype.vue` 做了单文件解析和模板编译检查，结果通过：`SFC compile ok`

# 追加记录：2026-04-08 接通第一批冷启动配置消费

## Git Commit Message

feat(settings): 接通首批冷启动配置的主程序消费

## Modification

- `server/src/main.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `server/src/main.cpp` 里补了中文注释，说明为什么 `http_port`、`kernel_worker_threads`、`token_limit`、`span_capacity`、`collecting_idle_timeout_ms`、`sweep_tick_ms` 这批值要在启动时只读一次快照。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么这里的优先级要收口成 `CLI > Settings > 默认值`，以及为什么线程池大小不能做成运行中热更新。

## Learning Tips

### Newbie Tips

冷启动配置的关键不是“能不能从 repo 读出来”，而是“谁在什么时候第一次消费它”。如果一个值只在对象构造时才真正生效，那你把它存进数据库并不等于系统已经会用它，主程序的构造路径必须显式接上。

### Function Explanation

这次没有新增复杂抽象，只是在 `main.cpp` 启动期先拿 `config_repo->getSnapshot()`，然后把解析出来的最终值喂给 `InetAddress`、`ThreadPool`、`TraceSessionManager` 和 `loop.runEvery(...)`。

### Pitfalls

如果不区分“CLI 显式覆盖”和“只是沿用默认值”，你后面就很容易把库里的配置永远压死，或者反过来让脚本里显式传的参数失效。所以这里必须把“有没有显式传参”单独记下来，而不是只看最终的整数值。

## Verification

- 按当前步骤约束，这次没有运行编译和测试。

# 追加记录：2026-04-08 接通 sealed/retry 冷启动时序配置

## Git Commit Message

feat(trace): 接通 sealed 与 retry 时序配置的冷启动消费

## Modification

- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/src/main.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `server/core/TraceSessionManager.h` 里补了中文注释，说明为什么 `sealed_grace_window_ms` 与 `retry_base_delay_ms` 要在构造期折算成 tick，以及为什么当前 sealed 窗口先统一成单值而不是按 reason 分档。
- 在 `server/core/TraceSessionManager.cpp` 里补了中文注释，说明为什么 delay 要向上取整到至少 1 tick，以及为什么 retry 退避要改成“base tick * 指数倍数”的形式。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么 sealed/retry 这两个值当前只走 Settings 冷启动配置，不再继续保留内部硬编码。

## Learning Tips

### Newbie Tips

时间轮真正调度的是 tick，不是毫秒。所以你把配置项设计成毫秒之后，真正容易出 bug 的地方不是“值怎么存”，而是“什么时候换算成 tick、向上还是向下取整、0 tick 怎么处理”。

### Function Explanation

这次把 `ComputeSealDelayTicks(...)` 和 `ComputeRetryDelayTicks(...)` 从硬编码改成配置驱动：构造函数先把毫秒值按 `wheel_tick_ms_` 换算成基础 tick，后面 sealed 和 retry 状态只复用缓存结果，不再每次临时算。

### Pitfalls

如果直接用整数除法把毫秒配置换算成 tick，而不做向上取整，那么当 `sealed_grace_window_ms < wheel_tick_ms_` 时，结果会直接掉成 0 tick，表现出来就是“刚 seal 就立刻到期”，这种 bug 非常隐蔽。

## Verification

- 按当前步骤约束，这次没有运行编译和测试。

# 追加记录：2026-04-08 接通 webhook channels 的冷启动消费

## Git Commit Message

feat(notification): 接通 webhook 渠道配置的冷启动消费

## Modification

- `server/notification/NotifyTypes.h`
- `server/notification/WebhookNotifier.cpp`
- `server/src/main.cpp`
- `server/tests/WebhookNotifier_test.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `server/notification/NotifyTypes.h` 里补了中文注释，说明为什么 `WebhookChannel` 也要带 `threshold`，不能只消费地址和密钥。
- 在 `server/notification/WebhookNotifier.cpp` 里补了中文注释，说明为什么真正的风险阈值过滤要放在 notifier 层做，而不是继续把 `alert_threshold` 当摆设。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么 settings 里的渠道要在启动时投影成 notifier channels，以及为什么 CLI webhook 入口现在只保留为调试兜底。
- 在 `server/tests/WebhookNotifier_test.cpp` 里补了中文注释，说明新测试锁定的是“warning 事件不能再命中 critical-only 渠道”这条真实配置语义。

## Learning Tips

### Newbie Tips

配置“能存”和配置“真生效”是两回事。像 `alert_threshold` 这种字段，如果只在 SQLite 里有、前端也能改，但通知层从来不读，那它本质上就是假功能。

### Function Explanation

这次没有把 notifier 改成热更新快照，而是先做冷启动消费：`main.cpp` 启动时把 `settings.channels` 映射成 `WebhookChannel`，再交给 `WebhookNotifier`；真正的阈值判断放在 `notifyTraceAlert(...)` 里做。

### Pitfalls

如果只把 settings 里的 `webhook_url/secret` 接上，却不把 `alert_threshold` 一起接真，那么页面上看起来能配 `warning/error/critical`，实际发送行为却永远一样。这种“半真半假”比完全没做更容易误导联调。

## Verification

- 运行 `cmake --build server/build --target test_webhook_notifier && ./server/build/test_webhook_notifier --gtest_filter=WebhookNotifierTest.NotifyTraceAlertHonorsChannelThreshold`
- 运行 `./server/build/test_webhook_notifier`
- 运行 `cmake --build server/build --target LogSentinel`

# 追加记录：2026-04-08 收口 trace_end 冷启动解析口径

## Git Commit Message

refactor(trace): 将 trace_end 解析口径收口为冷启动配置

## Modification

- `server/handlers/LogHandler.h`
- `server/handlers/LogHandler.cpp`
- `server/src/main.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `server/handlers/LogHandler.h` 里补了中文注释，说明为什么 `trace_end_field` 与 `trace_end_aliases` 不再按请求读快照，而是改成构造期注入。
- 在 `server/handlers/LogHandler.cpp` 里补了中文注释，说明为什么要在构造期预展开 `trace_end_known_fields_`，以及为什么请求里直接复用这份冷启动口径。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么 `trace_end` 主字段和别名也改成冷启动配置，不再继续走热更新。

## Learning Tips

### Newbie Tips

“技术上能热更新”不等于“业务上应该热更新”。如果一个配置改变的是协议解释口径，而不是普通展示或任务参数，那么运行中切它的收益通常很低，排查成本却很高。

### Function Explanation

这次把 `LogHandler` 里原来每个 `/logs/spans` 请求都做一次 `config_repo->getSnapshot()` 的路径删掉了，改成启动时在 `main.cpp` 算出 `effective_trace_end_field / effective_trace_end_aliases`，再一次性注入给 `LogHandler`。

### Pitfalls

如果你一边把某个字段定义成“冷启动配置”，一边又让热路径在每次请求里回头读 repo，那这个语义就是假的。最后系统行为会变成“文档说要重启，实际上不重启也会变”，这种口径漂移比单纯没实现更坑。

## Verification

- 按当前步骤约束，这次没有运行编译和测试。

# 追加记录：2026-04-08 接通背压水位阈值的冷启动消费

## Git Commit Message

feat(trace): 接通背压水位阈值的冷启动消费

## Modification

- `server/core/TraceSessionManager.h`
- `server/core/TraceSessionManager.cpp`
- `server/src/main.cpp`
- `docs/todo-list/Todo_Settings_MVP5.md`
- `docs/dev-log/20260408-feat-settings-prototype.md`

## 这次补了哪些注释

- 在 `server/core/TraceSessionManager.h` 里补了中文注释，说明为什么三组水位阈值当前只开放 overload/critical 两档，以及为什么 `low` 仍由后端内部派生。
- 在 `server/core/TraceSessionManager.cpp` 里补了中文注释，说明为什么这三组阈值要在构造期接入状态机，以及为什么 `low_percent` 固定按 overload 往下退 10 个百分点做回滞。
- 在 `server/src/main.cpp` 里补了中文注释，说明为什么水位阈值也属于冷启动配置、为什么这里只校验百分比区间合法性，不在启动期再引入更多热更新分支。

## Learning Tips

### Newbie Tips

背压阈值不是单纯“几个数字好看就行”。既然它们会直接决定请求什么时候开始拒绝、什么时候回到正常态，那么它们就是状态机参数，不是普通展示配置，默认先按冷启动处理更稳。

### Function Explanation

这次把 `BuildWatermark(...)` 从固定 55/75/90 改成了配置驱动版本：启动时只读取 overload/critical 百分比，构造 `TraceSessionManager` 时再换算成 `low/high/critical` 三个绝对值阈位。

### Pitfalls

如果把三个阈值都直接暴露给前端，用户很容易把 `low >= high` 或 `high > critical` 这种非法组合保存进去。当前先只开放 overload/critical 两档，`low` 交给后端内部派生，能少掉一类很低价值的错误输入。

## Verification

- 按当前步骤约束，这次没有运行编译和测试。
