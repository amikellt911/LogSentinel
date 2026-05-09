Git Commit Message
fix(frontend): 修复前端中英文文案混用

Modification
- client/src/i18n.ts
- client/src/components/TraceListTable.vue
- client/src/components/AIAnalysisDrawerContent.vue
- client/src/views/TraceExplorer.vue
- client/src/views/SettingsPrototype.vue
- docs/todo-list/Todo_frontend_i18n_cleanup.md

Learning Tips
Newbie Tips
- 前端国际化最怕的不是“没切语言”，而是同一个业务状态在不同组件里各写一套字符串。这样一旦补英文，只会补到一半，最后用户看到的就是中英文串台。
- 后端返回的 `ai_status` 这类字段本质上是协议枚举，不应该直接当 UI 文案用。前端应该先做“协议值 -> 翻译 key -> 展示文本”的映射。
- 原型页如果历史上大量写死文案，不一定要一口气全量国际化。先优先收口用户最常操作、最容易感知混用的问题路径，风险更可控。

Function Explanation
- `useI18n()` 用来在 `script setup` 里拿到 `t()`，这样函数里的状态提示、错误消息、确认弹窗也能走翻译表，而不只是模板里的 `$t(...)`。
- `i18n.global.t(...)` 适合在当前文件没有直接用 Composition API i18n 上下文，或者在普通工具函数里需要即时取翻译时使用。
- `ElMessageBox.confirm(...)` 的按钮文案和标题如果不手动传翻译文本，就很容易和页面当前语言不一致。

Pitfalls
- 只改模板里的标签不够，真正容易漏的是 `ElMessage.success/error/warning`、`ElMessageBox.confirm`、动态默认名称、状态映射函数这些脚本里的字符串。
- 同一状态如果在列表组件和详情组件各自写 switch，很快就会漂移。哪怕这次不抽公共 util，至少也要共用同一批 i18n key。
- 跑通 `npm run build` 很关键，因为 Vue 模板里改了绑定、脚本里加了 `t()` 之后，最容易出的问题就是 key 写错、导入漏掉、类型没对上。

追加记录
Modification
- client/src/components/TraceSearchBar.vue
- client/src/views/ServiceMonitorPrototype.vue

Newbie Tips
- 做国际化补漏时，先看当前正式路由比先全仓库搜索更重要。否则很容易把已经废弃、不会暴露给用户的页面也一起拖下水，改动面会失控。
- `ServiceMonitorPrototype.vue` 这种“原型页转正”的页面最容易留下历史写死文案，因为它最初通常不是按完整产品页标准搭的。

Pitfalls
- `TraceSearchBar` 这种筛选组件里，`el-option` 的 `label` 很容易被忽略，因为它们不是普通文本节点，但对语言切换同样可见。
- 正式入口页和旧废页如果并存，一定要先以 `router/index.ts` 为准确认谁还在用，不要凭文件名猜。

20260509 追加记录
Modification
- client/src/views/SettingsPrototype.vue
- client/src/i18n.ts

Newbie Tips
- 原型页转正后，最容易漏的是区块标题、按钮文案和 badge 状态词，因为这些文本通常散在模板里，不像 `ElMessage` 那样会一眼想到去搜函数调用。
- 像 `SettingsPrototype` 这种页面，如果暂时不想把所有词条都并进旧 `settings.*` 树，可以先用一个集中 `computed labels` 收口。这样至少先把硬编码拔掉，后续再慢慢并词条结构。

Pitfalls
- `Primary/Fallback` 这种短词特别容易漏，因为开发时会把它们当“技术术语”而不是 UI 文案，但用户看到的就是界面文本，照样要跟语言切换。
- 只看模板正文不够，`el-form-item label`、tab label、section title、empty state、按钮文字都要一起排查，否则页面还是会在切换语言时出现零星串台。

20260509 再追加
Modification
- client/src/views/SettingsPrototype.vue
- client/src/i18n.ts

Newbie Tips
- 国际化扫尾时，placeholder 和 `el-switch` 的 `active-text/inactive-text` 特别容易被漏掉，因为它们不是主标题，但用户一操作就会直接看到。
- 如果一页里还留着很多“示例输入文案”，不要硬塞回业务逻辑里判断语言，继续复用前面那组集中 label/computed 才不容易越修越散。

Pitfalls
- `template #suffix` 里的单位，比如这里的 `s`，本质上也是 UI 文案。如果前面都翻了，单位没翻，页面看起来还是会很别扭。
- `Feishu / Lark`、`term / meaning` 这种看起来像“固定术语”的字段，如果它是给用户看的占位文本或只读值，同样要按界面语言检查，而不是默认当成不用翻。

20260509 最后追加
Modification
- client/src/views/SettingsPrototype.vue
- client/src/i18n.ts

Newbie Tips
- 默认 seed 数据里如果有“名称型字段”，比如默认 Prompt 名称、默认渠道名，它们虽然不是接口返回的真实业务数据，但依然会直接出现在正式页面上，所以也会影响语言切换观感。
- 这类 seed 名称可以先国际化；但更长的业务示例内容要不要翻，最好单独判断，不要为了追求“全翻”把业务示例和 UI 壳一次性搅在一起。

Pitfalls
- 名称型 seed 数据和示例内容要分开处理。前者属于显式 UI 列表项，优先级高；后者更像演示素材，贸然全改会让改动面失控。
- 国际化扫尾时，别忘了本地默认值路径。哪怕后端正常会回填，加载失败或空数据回退时，用户照样可能先看到这些本地 seed 文案。

20260509 收尾追加
Modification
- client/src/views/SettingsPrototype.vue
- docs/todo-list/Todo_frontend_i18n_cleanup.md

Newbie Tips
- 如果语言切换要兼顾“默认示例跟着变”以及“用户手工编辑不能被覆盖”，最稳的办法不是无脑重刷整份表单，而是拿旧语言默认值做哨兵，只同步那些还没被用户改动过的字段。
- 这种默认 seed 内容最好抽成纯函数或常量映射，不要把一堆中英文字符串直接塞进 `watch` 里。否则后面一改文案，比较逻辑和赋值逻辑很容易一起漂掉。

Pitfalls
- 像 `{ value: xxx }` 这种临时包装对象，如果没有把结果真正写回响应式源字段，就只是看起来“调用了同步函数”，实际 Vue 状态完全没变。
- 原型页收尾时，别只顾着修脚本里的语言切换；页头、表单标题这类高频可见硬编码如果还留着，用户第一眼还是会觉得页面没修干净。
