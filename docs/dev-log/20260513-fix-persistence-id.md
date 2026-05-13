# 20260513-fix-persistence-id

## 1. 任务背景

用户反馈在前端设置页保存飞书渠道配置后，虽然页面能立刻看到数据，但是如果关闭后端并重启，配置就会丢失。经过排查确认，由于前端给新建记录分配了大于 0 的临时 ID（比如 1、2），后端在 `SqliteConfigRepository::handleUpdateChannel` 和 `handleUpdatePrompt` 收到该数据时，单纯地因为 ID > 0 就判断它是一条旧记录，进而构造 `UPDATE` 语句。由于数据库里并不存在该 ID，受影响行数（changes）为 0，这在 SQLite 中不会抛出异常。随后缓存更新成功并 COMMIT，导致了“幽灵数据”——内存里有，但没落盘。

## 2. 解决方案推导

为了在不打破前端单页应用（SPA）依赖 ID 维护 UI 状态的前提下实现完美闭环，我们放弃了“前端强制传 0”的方案，而是将后端升级为了兼容前端 ID 的类 **UPSERT** 语义：
- 当识别到 `ID > 0` 时，依然先尝试 `UPDATE`。
- 关键在于紧接一个 `sqlite3_changes(db_)` 检查。如果受影响行数为 0，说明这个 ID 是前端为了 UI 组件捏造的新 ID，并不是库里的旧数据。
- 此时将状态回退，流转到 `INSERT` 分支。
- 在 `INSERT` 时，如果传来了 ID，就显式地通过 `sqlite3_bind_int` 绑定该主键 ID 写入，强行把前端分配的 ID 固化到数据库中。

这样一方面避免了前后台 ID 映射带来的渲染跳动和高亮丢失，另一方面也确保了所有数据都能踏实落盘。通过排查，由于 `app_config`、`ai_provider_profiles` 和 `trace_end_aliases` 采用的都是真正意义上的 `ON CONFLICT DO UPDATE` 或者暴力覆写（DELETE + INSERT），所以同类隐患仅存在于 `channels` 和 `prompts`，现已双双修复。

## 3. Git Commit Message

```text
fix(persistence): 修复前端分配新 ID 导致更新失效的幽灵数据问题

- 修复 `handleUpdateChannel` 在遇到前端捏造的未知 ID 时 `UPDATE` 静默影响 0 行导致未落盘的问题。
- 修复 `handleUpdatePrompt` 中相同逻辑的隐患。
- 补充检查 `sqlite3_changes == 0` 并 fallback 到显式绑定 ID 进行 `INSERT`，确保前后端主键同频且数据不丢失。
```

## 4. Modification

- `server/persistence/SqliteConfigRepository.cpp`:
  - `handleUpdateChannel`
  - `handleUpdatePrompt`

## 5. Learning Tips

### Newbie Tips
在单页应用（SPA）中，前端在本地创建新条目时通常需要立即分配一个临时 ID（例如为了 Vue 的 `v-for key` 或高亮态绑定），这与传统的表单递交后由数据库自增返回 ID 的模式不同。后端在设计写入接口时，需要预料到可能会收到库里不存在的正数 ID，不能简单地用 `if (id > 0)` 就盲目断定其为 UPDATE，必须通过 `sqlite3_changes` 结合 fallback，或者原生的 UPSERT 语法来兜底。

### Function Explanation
- `sqlite3_changes(sqlite3*)`：返回最近一次成功完成的 INSERT、UPDATE 或 DELETE 语句所修改、插入或删除的行数。这是排查“SQL 执行没报错但数据就是没改”的利器。如果通过 `sqlite3_step` 执行 UPDATE 后返回了 `SQLITE_DONE`，但 `sqlite3_changes` 为 0，就意味着 WHERE 条件没匹配上任何行。

### Pitfalls
- **SQLITE_DONE 的欺骗性**：很多人觉得只要 `sqlite3_step` 没报错并返回了 `SQLITE_DONE`，数据就一定进去了。实际上，一条完全合法的 `UPDATE t SET v=1 WHERE id=9999` 如果碰上空表，依然是完美的 `SQLITE_DONE`。所以对于 UPDATE 操作，**必须**检查受影响行数，或者使用 `INSERT ... ON CONFLICT DO UPDATE` 来确保原子化的 UPSERT 语义。