# API 接口优化分析：批量更新策略 (API Refinement)

本文档针对 `Settings` 模块的更新接口进行深度分析，对比“单条更新”与“批量更新”的优劣，并给出最终设计建议。

## 1. 核心问题

目前的初步设计是：
*   `POST /api/settings/prompt` (接收单个对象)

**您的疑问:**
> "我觉得应该发送一个json array吧？...一次发送一整批感觉比发送一批发送一个个要好的多吧？"

**我的回答:**
**完全正确。对于“Prompt 列表”和“通知渠道列表”这种数据量不大（通常几十条以内）的场景，批量发送（Sync Strategy）是绝对的最优解。**

---

## 2. 方案对比

### 方案 A: 单条更新 (The "RESTful" way)
前端检测到变动，循环发送 N 个请求，或只发变更的那个。
*   `POST /prompts` (新增)
*   `PUT /prompts/:id` (修改)
*   `DELETE /prompts/:id` (删除)

*   **缺点:**
    1.  **一致性难保证:** 如果用户删了一个 Prompt 又加了一个 Prompt，操作顺序反了可能导致 ID 冲突或业务逻辑错误。如果中间断网了，状态就不一致了。
    2.  **前端复杂:** 前端需要精细地追踪哪个 ID 被删了，哪个是新的（临时 ID），哪个是脏的。
    3.  **交互复杂:** 对于“保存”按钮这种一次性提交的交互，发 N 个请求很难处理 Loading 状态和错误回滚。

### 方案 B: 批量全量同步 (The "Batch Sync" way) - **推荐**
前端直接把当前 Store 里最新的列表整个发给后端。
*   `POST /api/settings/prompts` (Body: `[ ...list... ]`)

*   **优点:**
    1.  **事务原子性:** 后端开启一个事务，要么全更新成功，要么全失败。数据库永远不会处于“一半新一半旧”的状态。
    2.  **前端极简:** 前端不需要计算 Diff（不需要管是新增还是修改），直接 `JSON.stringify(prompts)` 发出去就行。
    3.  **处理删除:** 这是单条更新最头疼的。在全量模式下，**凡是不在请求列表里的 ID，就是需要被删除的。** 逻辑非常清晰。

---

## 3. 推荐的接口设计

根据您的建议，我们修正接口定义：

### 3.1 提示词列表 (Prompts)
*   **Endpoint:** `POST /api/settings/prompts` (注意是复数)
*   **Payload:**
    ```json
    [
        { "id": 1, "name": "Audit", "content": "...", "is_active": true },
        { "id": 0, "name": "New One", "content": "...", "is_active": true } // id=0 表示新增
    ]
    ```

### 3.2 通知渠道列表 (Channels)
*   **Endpoint:** `POST /api/settings/channels`
*   **Payload:**
    ```json
    [
        { "id": 1, "name": "DingTalk", "webhook_url": "...", "is_active": true }
    ]
    ```

---

## 4. 后端实现策略 (C++ Logic)

后端收到 List 后，如何处理？

由于数据量很小（通常 < 100 条），我们不需要复杂的 Diff 算法。最稳健的策略是 **"Sync Replace"**。

**伪代码逻辑 (`SqliteConfigRepository`):**

```cpp
void updateAllPrompts(const std::vector<PromptConfig>& new_prompts) {
    // 1. 开启事务
    db.exec("BEGIN TRANSACTION");

    try {
        // 策略：
        // A. 找出需要删除的 (在 DB 中存在但不在 new_prompts 中的 ID)
        // B. 找出需要更新的 (ID > 0)
        // C. 找出需要新增的 (ID == 0)

        // 或者更暴力的 "清空重写" (仅当无外键引用时安全，ID会变)
        // 但为了保留 ID 的稳定性（日志表可能引用了 Prompt ID），我们通常采用 "Smart Sync":

        std::set<int> new_ids;
        for(auto& p : new_prompts) if(p.id > 0) new_ids.insert(p.id);

        // 1. 删除不存在的
        db.exec("DELETE FROM ai_prompts WHERE id NOT IN (new_ids...)");

        // 2. 更新或插入
        for (const auto& p : new_prompts) {
            if (p.id > 0) {
                // UPDATE ... WHERE id = p.id
            } else {
                // INSERT INTO ...
            }
        }

        db.exec("COMMIT");
    } catch (...) {
        db.exec("ROLLBACK");
        throw;
    }
}
```

### 4.1 哈希校验优化 (可选)
您提到的 *"通过哈希值来判断是否发送变化"*：
*   **前端:** 可以在保存时，计算 `hash(current_list)` vs `hash(synced_list)`。如果哈希一样，说明根本没变，**直接不发送请求**。这是非常好的前端优化。
*   **后端:** 没必要做哈希。因为既然请求发过来了，说明前端认为有变化，直接执行数据库更新即可（SQLite 非常快）。

---

## 5. 总结

您提出的 **"发送 JSON Array"** 和 **"批量更新"** 是完全正确的架构决策。它大大简化了前后端的复杂度，并利用数据库事务保证了数据的一致性。
后端 C++ 接口函数应调整为：

```cpp
// 接受列表
void handleUpdatePrompts(const std::vector<PromptConfig>& prompts);
void handleUpdateChannels(const std::vector<AlertChannel>& channels);
```
