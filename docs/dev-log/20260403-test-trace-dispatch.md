Git Commit Message:
test(core): 修正trace异步分发后的回滚单测口径

Modification:
- server/tests/TraceSessionManager_unit_test.cpp
- docs/todo-list/Todo_TraceSessionManager.md

Learning Tips:

Newbie Tips:
- 把同步逻辑改成异步后，最容易先炸掉的不是业务代码本身，而是测试里的“时序假设”。以前 `SweepExpiredSessions()` 返回时 session 已经同步回滚，现在改成独立 dispatch 线程后，sweep 返回只代表 job 已经投出去，不代表回滚已经完成。
- 这类测试不要写固定 `sleep`，而要等“状态真正出现”。因为线程调度和机器负载都会变，固定睡眠只会让单测偶现抖动。

Function Explanation:
- `WaitUntil(...)`：这里用短轮询包住异步条件，作用是把“线程什么时候跑到”转换成“状态什么时候满足”。只要条件函数写得够具体，测试就会比硬编码等待时间稳定很多。
- `std::function<bool(const TraceSession&)>`：这里把每条用例真正关心的状态判断抽成 predicate，避免把“等待 session 出现”和“断言 session 内容”重复写 7 遍。

Pitfalls:
- 不能只等 `submit_fail_count` 这类原子计数。因为 dispatch 线程里是先 `submit_fail_count++`，后面才拿 manager 锁做 `RestoreSessionLocked()`；如果测试只看计数，就会在“计数变了但 session 还没塞回去”的时间缝里误判失败。
