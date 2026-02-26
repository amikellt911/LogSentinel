# 20260226 refactor-notification

## Git Commit Message
`refactor(notification): 移除notifyReport并收敛INotifier依赖边界`

## Modification
- `server/notification/INotifier.h`
- `server/notification/WebhookNotifier.h`
- `server/notification/WebhookNotifier.cpp`
- `server/core/LogBatcher.cpp`
- `docs/todo-list/Todo_WebhookNotifier.md`

## Learning Tips

### Newbie Tips
- 抽象接口文件应该尽量只暴露能力，不要直接引入下层实现或业务聚合结构，否则会出现“上层依赖下层”的反向耦合。
- 没有真实调用的接口要尽早删除，避免后续新功能继续围绕无效抽象扩展，导致重构成本持续上升。

### Function Explanation
- `virtual void notify(...) = 0;`：纯虚函数定义了通知能力的最小契约，让调用方只依赖接口，不依赖具体实现。
- `rg -n "notifyReport\(" ...`：用全局检索确认接口删除后没有遗漏调用点，这是删除公共接口时最关键的回归检查手段。

### Pitfalls
- 只删实现不删接口声明会导致“接口仍承诺能力，但实现已缺失”的编译错误或行为不一致。
- 只删 `INotifier` 不同步 `WebhookNotifier` 的 `override` 会直接触发签名不匹配编译失败。
- 删除接口后不做至少一次全量编译，容易把错误留到后续 unrelated 改动里才暴露，排查成本更高。
