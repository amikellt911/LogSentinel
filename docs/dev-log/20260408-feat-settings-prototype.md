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
