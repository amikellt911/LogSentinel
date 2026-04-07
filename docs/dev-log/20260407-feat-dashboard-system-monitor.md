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

# 追加记录：2026-04-07 背压卡去掉 queue 百分比副文案

## Git Commit Message

refactor(frontend): 移除系统监控背压卡的队列百分比副文案

## Modification

- `client/src/views/Dashboard.vue`
- `docs/todo-list/Todo_Phase1_ServiceMonitor.md`
- `docs/dev-log/20260407-feat-dashboard-system-monitor.md`

## 这次补了哪些注释

- 在 `client/src/views/Dashboard.vue` 的背压状态计算附近补了中文注释，说明为什么这张卡现在只显示综合状态，不再附带单一队列百分比。

## Learning Tips

### Newbie Tips

如果后端某个状态是“多因素联合判断”出来的结论，前端就不要再随手拿其中一个局部因子当副文案。否则页面会出现一种很怪的情况：主标题说的是综合状态，副文案却像在暗示它只由一个队列决定。

### Function Explanation

这里删掉的是 `MetricCard` 的 `subValue`，不是把背压卡整个删掉。主值仍然来自 `backpressureStatus`，只是把容易误导的补充说明拿掉。

### Pitfalls

`queuePercent` 这个字段当前仍可能保留在 store 或旧接口里，但这不代表它适合继续展示。展示层应该优先服从当前页面语义，而不是为了“字段已经有了”就硬塞进 UI。
