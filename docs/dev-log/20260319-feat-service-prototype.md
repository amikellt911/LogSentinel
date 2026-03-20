Git Commit Message:
feat(client): 新增服务监控原型页

Modification:
- docs/todo-list/Todo_Phase1_ServiceMonitor.md
- client/src/views/ServiceMonitorPrototype.vue
- client/src/router/index.ts
- client/src/layout/MainLayout.vue
- client/src/i18n.ts

Learning Tips:

Newbie Tips:
- 原型页和正式页不要混着改。既然这一步只是验证“页面职责和信息结构”，那么最稳的做法就是新开一个独立视图，把旧页面完整保留，这样你随时可以横向对比，不会把旧功能一起带崩。
- Vue 里做这种页面原型，先用本地 mock 数据把交互骨架跑起来，比一开始就接后端更清楚。因为你现在要验证的是“用户先看什么、后看什么”，不是接口细节。

Function Explanation:
- `computed`
  这里用来派生“当前选中的服务”“顶部总览卡片”“操作排行”。既然这些值都能从一份原始 mock 数据推出来，那么就不该手动维护多份状态，否则一改一处就容易漏同步。
- `router.push`
  聚焦面板里的“进入 Trace 查询页”直接跳 `/traces`。既然这个原型页的职责是先定位服务，再跳详情，那么路由跳转本身就是页面分工的一部分。

Pitfalls:
- 原型页最容易犯的错就是继续堆 trace 列表。既然你想让它和 TraceExplorer 分开，那么右侧主体就不能再做一串事件卡片，否则视觉上还是“另一个查询页”。
- 热力图这类图很容易做成纯装饰。所以这次没有用莫名其妙的双服务交叉矩阵，而是改成“服务 x 风险维度”，这样答辩时至少能一句话解释清楚横轴和纵轴分别代表什么。

---

追加修改（2026-03-19）：

Git Commit Message:
refactor(client): 调整服务监控原型页信息结构

Modification:
- client/src/views/ServiceMonitorPrototype.vue
- docs/todo-list/Todo_Phase1_ServiceMonitor.md
- docs/dev-log/20260319-feat-service-prototype.md

Learning Tips:

Newbie Tips:
- 如果一个图表连你自己都得先解释半天才能看懂，那它大概率就不适合首页。首页更适合放“最近 3 条异常样本”这种直接能落到具体问题上的东西。

Pitfalls:
- “服务风险矩阵”这种组件真正难的不是前端画格子，而是后端得先给出稳定的分类口径。既然当前还没有把“数据库异常、超时、下游依赖问题”这些维度定义扎实，就不要让原型被这种图绑住。

---

追加修改（2026-03-19，文案收口）：

Git Commit Message:
refactor(client): 统一服务监控原型页操作语义

Modification:
- client/src/views/ServiceMonitorPrototype.vue
- docs/dev-log/20260319-feat-service-prototype.md

Learning Tips:

Newbie Tips:
- 如果后端现在稳定有的是 `span.name`，那前端就该把展示语义收口成“操作”，不要提前写成“接口”或“路由”。既然语义还没到那一步，名字就别抢跑。

Pitfalls:
- “接口”“路由”“操作”这三个词不能混着叫。现在原型页如果继续写“异常接口排行”，用户会自然以为后端已经稳定拿到了 HTTP route；但你们当前真正稳的是 `span.name`，所以这里必须改成“异常操作排行”。

---

追加修改（2026-03-19，页面继续裁剪）：

Git Commit Message:
refactor(client): 裁剪服务监控原型页重复模块

Modification:
- client/src/views/ServiceMonitorPrototype.vue
- docs/todo-list/Todo_Phase1_ServiceMonitor.md
- docs/dev-log/20260319-feat-service-prototype.md

Learning Tips:

Newbie Tips:
- 页面原型不怕减法做狠一点。既然“当前最危险服务”已经能从左侧排序和右侧聚焦面板看出来，那它就不该继续占一个总览卡位。

Pitfalls:
- mini trend 这种元素很容易让页面看起来更满，但它如果不能帮助用户更快定位问题，就只是在制造视觉噪声。当前这页的重点是服务、问题摘要和异常样本，不是画一堆小折线。

---

追加修改（2026-03-19，字段继续收口）：

Git Commit Message:
refactor(client): 收口服务监控原型页字段展示

Modification:
- client/src/views/ServiceMonitorPrototype.vue
- docs/dev-log/20260319-feat-service-prototype.md

Learning Tips:

Newbie Tips:
- 左侧列表负责“快速定位”，右侧面板负责“展开解释”。既然如此，平均耗时这类需要一点阅读成本的指标就更适合放右侧，不应该在左侧卡片里重复占位。

Pitfalls:
- Trace 样本卡里的“触发原因”很容易和“摘要”说同一件事。既然这一步的目标是给用户一个样本入口，而不是再塞一层解释，那就应该把重复字段删掉，保留时间、风险、操作、摘要和整链路耗时就够了。

---

追加修改（2026-03-19，布局收口）：

Git Commit Message:
refactor(client): 调整服务监控原型页布局层级

Modification:
- client/src/views/ServiceMonitorPrototype.vue
- docs/dev-log/20260319-feat-service-prototype.md

Learning Tips:

Newbie Tips:
- 当左右两块内容高度差很大时，不要硬用两行独立 grid 去对齐。既然左列和右列承担的是两条不同的信息路径，就应该把它们改成“各自纵向堆叠”，这样页面阅读顺序和视觉留白都会更自然。

Pitfalls:
- 监控页最容易出现的丑问题不是颜色，而是空白。左侧下一个模块如果必须等右侧大面板结束后才能开始，那你就会看到一整块黑洞式空白；这通常说明布局分组错了，不是内容本身有问题。

---

追加修改（2026-03-20，界面文案收口）：

Git Commit Message:
refactor(client): 明确服务监控原型页统计口径文案

Modification:
- client/src/views/ServiceMonitorPrototype.vue
- docs/dev-log/20260319-feat-service-prototype.md

Learning Tips:

Newbie Tips:
- 页面字段一旦涉及“次数”“耗时”“时间”，最好直接把口径写进小字说明里。既然这些数字背后都有去重规则和取值规则，那就别指望用户自己脑补。

Pitfalls:
- “高风险事件数”“异常次数”“平均耗时”这种词如果不加限定，页面看起来像懂了，实际上每个人脑子里想的口径都不一样。原型阶段就应该把名字先改正，不要等后端做完再返工。

---

追加修改（2026-03-20，请求/状态语义收口）：

Git Commit Message:
refactor(client): 收口服务监控原型页请求与状态语义

Modification:
- client/src/views/ServiceMonitorPrototype.vue
- docs/dev-log/20260319-feat-service-prototype.md

Learning Tips:

Newbie Tips:
- 在你们这个页面里，一条 trace 更适合解释成“一次请求链路”，所以计数字段写成“异常请求数”会比“异常事件数”更贴用户直觉。

Pitfalls:
- 如果当前没有稳定的服务级风险评分，就不要硬写“高风险/中风险/低风险”。先收口成“异常 / 稳定”两档，页面语义才不会跑偏。
