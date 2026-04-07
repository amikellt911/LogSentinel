# Git Commit Message

refactor(frontend): 收口系统监控页面骨架与文案

# Modification

- `client/src/views/Dashboard.vue`
- `client/src/components/TokenMetricsCard.vue`
- `client/src/i18n.ts`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

# 这次补了哪些注释

- 在 `client/src/views/Dashboard.vue` 里补了中文注释，说明这页当前先收口成“系统运行态页”，只保留系统指标卡、Token 卡和两条吞吐曲线。
- 在 `client/src/components/TokenMetricsCard.vue` 里补了中文注释，说明为什么去掉“节省比例 / 预估成本”，改成纯 token 真值展示。

# Learning Tips

## Newbie Tips

系统监控页和服务监控页不要混着做。前者看的是进程自身的运行态，例如队列等待、推理延迟、内存、背压和吞吐；后者看的是业务服务最近有没有出问题。两者如果混在一个页面里，用户会分不清自己到底是在看系统瓶颈还是业务异常。

## Function Explanation

`MetricCard` 这种通用卡片组件很适合先收语义、后接真值。也就是先把标题、副文案和布局收成你真正想要的系统口径，后面再把 `value` 换成后端快照，不要一边改文案一边改接口，容易把问题搅在一起。

## Pitfalls

“节省比例”“预估成本”这种文案在演示时很容易被追问计算口径。一旦当前页面还没有稳定的真实单价、provider 映射和 token 统计来源，这类卡片宁可先删，不要继续带着 mock 味很重的数字留在系统监控首页。
