# Git Commit Message

refactor(frontend): 收口设置正式入口并补单入口部署目标

# Modification

- `client/src/router/index.ts`
- `client/src/layout/MainLayout.vue`
- `CurrentTask.md`
- `docs/todo-list/Todo_Settings_MVP5.md`

# What Changed

- 将正式设置入口统一收口到 `/settings -> SettingsPrototype`。
- 保留 `/settings-prototype -> /settings` 重定向，兼容旧书签与联调阶段遗留地址。
- 从左侧导航移除独立的 `settings-prototype` 菜单项，避免继续暴露双设置页语义。
- 在 `CurrentTask.md` 中补入“后端托管 `client/dist`，形成单入口部署”的 v1.0.0 目标，并标记 `/settings` 单入口已完成。

# Verification

- `git diff --check`
- `cd client && npm run build`

说明：
- `git diff --check` 已通过，没有新增格式问题。
- `npm run build` 仍失败，但失败点是既有前端 TS 历史债，不是本次路由/侧边栏收口引入的新问题。
- 当前已确认仍存在的旧错误文件：
  - `client/src/components/AIEngineSearchBar.vue`
  - `client/src/components/BatchArchiveList.vue`
  - `client/src/components/BusinessHealthCards.vue`
  - `client/src/components/PromptDebugger.vue`
  - `client/src/components/RiskDistribution.vue`
  - `client/src/components/TraceWaterfall.vue`
  - `client/src/views/AIEngine.vue`
  - `client/src/views/Settings.vue`

# Learning Tips

## Newbie Tips

- SPA 正式部署通常只有一个 `index.html`。像 `/settings`、`/service` 这种路径不是不同 HTML 文件，而是同一个入口页面交给前端路由继续分发。
- 如果一个原型页已经转正，就不要继续把“旧页面 + 原型页”同时暴露给用户。双入口会把产品语义搞脏，后面联调和答辩都要反复解释。

## Function Explanation

- `redirect`：Vue Router 里的重定向规则。这里用它兼容旧地址，让历史书签自动跳回正式入口。
- `computed`：根据当前路由动态计算标题。这里保留它，是为了让顶部标题始终跟正式路由语义一致。

## Pitfalls

- 只改路由不改侧边栏，用户还是会从导航点进旧入口，等于没有真正收口。
- 只改前端入口但不更新任务文档，后面再做“单入口部署”时很容易忘掉这一步已经完成，导致文档和代码状态不一致。
