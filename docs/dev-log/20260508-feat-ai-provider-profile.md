# feat(ai): 增加 provider profile 配置表

## Git Commit Message

`feat(ai): 增加 provider profile 配置表`

## Modification

- `server/persistence/ConfigTypes.h`
- `server/persistence/SystemConfig.h`
- `server/persistence/SqliteConfigRepository.h`
- `server/persistence/SqliteConfigRepository.cpp`
- `server/handlers/ConfigHandler.h`
- `server/handlers/ConfigHandler.cpp`
- `server/src/main.cpp`
- `server/ai/proxy/providers/mock.py`
- `server/tests/ConfigRepository_profile_test.cpp`
- `server/tests/ai_proxy_trace_protocol_test.py`
- `server/tests/smoke_settings_blackbox.py`
- `server/CMakeLists.txt`
- `client/src/views/SettingsPrototype.vue`
- `docs/todo-list/Todo_TraceAiProvider.md`

## What Changed

- 新增 `ai_provider_profiles` 表，按 `provider -> model/api_key` 保存 `mock/gemini/glm/deepseek` 四套配置。
- `app_config` 只保留 `ai_provider` 和 `ai_fallback_provider`，旧 `ai_model / ai_api_key / ai_fallback_model / ai_fallback_api_key` 不再作为主来源，并在新库初始化时删除旧 key。
- `SystemConfig` 快照新增 provider profile map，`main.cpp` 启动期按主/备 provider 解析 model/key；运行时热更新版本线改为 profile 更新时 bump。
- Settings 页面改成四行 provider profile 编辑，provider label 统一为 `mock/gemini/glm/deepseek`。
- mock trace provider 增加保留测试 key：只有请求体 `api_key == 88888888` 才返回成功，防止误把 mock 当真实联通验证。

## Verification

- `cmake --build server/build`：通过。
- `./server/build/test_config_repository_profile`：2 条通过。
- `./server/build/test_trace_session_manager_unit`：54 条通过。
- `./server/build/test_trace_session_manager_integration`：14 条通过。
- `python3 server/tests/ai_proxy_trace_protocol_test.py -v`：15 条通过。
- `python3 -m py_compile server/tests/smoke_settings_blackbox.py server/ai/proxy/providers/mock.py server/ai/proxy/main.py`：通过。
- `npm run build`：通过，保留 Vite chunk size warning。
- `python3 server/tests/smoke_settings_blackbox.py --server-bin ./server/build/LogSentinel --ready-timeout 15 --dispatch-timeout 15`：执行到 provider profile 主备链路后，最终仍失败在既有 `trace_lifecycle_profile=` 日志等待点；本轮探针已看到 `glm-fake-model/glm-fake-key` 从 profile 进入请求体。

## Learning Tips

### Newbie Tips

- provider 路由和 model/key 是两类配置：路由决定请求发到 `/analyze/trace/{provider}`，model/key 只是请求体字段。前者运行中热切会改变对象行为，后者可以通过快照版本号热更新。
- `std::shared_ptr<const SystemConfig>` 发布快照时，内部 map 不应该原地改；更新时构造新快照再原子替换，读线程拿到的是稳定旧对象或稳定新对象。

### Function Explanation

- `std::atomic_store_explicit` / `std::atomic_load_explicit`：用于发布和读取共享快照，避免读线程看到半更新状态。
- SQLite `ON CONFLICT(provider) DO UPDATE`：用于 profile 增量 upsert，保存某个 provider 的 model/key 时不会误删其它 provider。
- `nlohmann::json` 的 `NLOHMANN_DEFINE_TYPE_INTRUSIVE`：让结构体自动支持 JSON 序列化和反序列化，Settings `/settings/all` 可以直接返回 profile map。

### Pitfalls

- 不能继续把 `ai_model` 放在全局 app_config 下。否则用户从 `gemini` 切到 `deepseek` 后，model 很可能还停在上一家的默认值，实际请求会变成 provider/model 错配。
- mock 的 `88888888` 不是安全机制，只是测试防误触。真正的安全边界仍然是不要提交真实 API Key，并且真实 provider 由各自厂商鉴权。
- 黑盒测试改 profile 后，热更新断言也必须改成写 `/settings/provider-profiles`，否则测试仍然只是在验证旧字段。

## 追加记录（TraceExplorer 单条删除）

### Git Commit Message

`feat(trace): 支持 TraceExplorer 单条删除`

### Modification

- `client/src/components/TraceListTable.vue`
- `client/src/views/TraceExplorer.vue`
- `server/CMakeLists.txt`
- `server/handlers/TraceQueryHandler.h`
- `server/handlers/TraceQueryHandler.cpp`
- `server/http/Router.cpp`
- `server/persistence/SqliteTraceRepository.h`
- `server/persistence/SqliteTraceRepository.cpp`
- `server/src/main.cpp`
- `server/tests/SqliteTraceRepository_test.cpp`
- `server/tests/TraceQueryHandler_test.cpp`
- `docs/todo-list/Todo_TraceExplorer_Delete.md`

### What Changed

- 给 `TraceExplorer` 列表操作列新增“删除”按钮，位置放在“查看详情”右侧，删除前弹二次确认。
- 后端新增 `DELETE /traces/{trace_id}`，直接复用 SQLite 仓储里的单条删除原语，不改 schema、不碰 Trace 聚合主链。
- 删除接口按幂等成功收口：目标 trace 已经不存在时，接口仍返回 `200`，前端直接刷新列表，不额外报错。
- 前端删除成功后会同步处理三件事：刷新列表、当前页删空时自动退上一页、如果删的是正在查看的 trace 就主动关闭详情抽屉。
- `Router` 的 CORS 预检补上 `DELETE`，避免浏览器在真正命中删除 handler 前被 `OPTIONS` 拦死。

### 中文注释补充位置

- `server/persistence/SqliteTraceRepository.cpp`
  - 在 `DeleteTraceById` 附近补了中文注释，说明为什么单条删除要按“幂等成功”收口。
  - 在 `HasTraceSummaryLocked` 附近补了中文注释，说明这里只做存在性判断，不重新走详情查询链。
- `server/handlers/TraceQueryHandler.cpp`
  - 在 `handleDeleteTrace` 里补了中文注释，说明为什么单条删除故意走同步事务，而不是复用 query 线程池异步返回。
- `server/http/Router.cpp`
  - 在 `OPTIONS` 预检响应里补了中文注释，说明 TraceExplorer 新增删除按钮后为什么必须放行 `DELETE`。
- `client/src/views/TraceExplorer.vue`
  - 在 `handleDeleteTrace` 里补了中文注释，说明“当前页删空退页”和“删除当前详情项时关闭抽屉”的状态收口原因。
- `client/src/components/TraceListTable.vue`
  - 在删除按钮事件附近补了中文注释，说明为什么必须阻止冒泡，避免点击删除时顺手触发行点击把详情抽屉打开。
- `server/tests/SqliteTraceRepository_test.cpp`
  - 新增测试旁补了中文注释，说明为什么“重复删除已不存在 trace”也要按成功语义验证。
- `server/tests/TraceQueryHandler_test.cpp`
  - 在 400/200 两组用例旁补了中文注释，说明路径参数校验和幂等删除的 HTTP 语义。

### Verification

- `cmake --build server/build --target test_sqlite_trace_repo LogSentinel`：通过。
- `./server/build/test_sqlite_trace_repo`：17 条通过。
- `cmake --build server/build --target test_trace_query_handler`：通过。
- `./server/build/test_trace_query_handler`：3 条通过。
- `npm run build`：通过，仍保留既有 chunk size warning，没有新增构建错误。

### Learning Tips

#### Newbie Tips

- “后端能删数据”和“前端删完状态不乱”是两件事。真正容易漏的是删完当前页空了怎么办、右侧详情还开着怎么办，而不是 SQL 里写不写 `DELETE`。
- 幂等删除不是在纵容脏请求，而是在降低用户态噪音。列表页重复点删除，最合理的体验就是“反正现在它没了”，而不是第二次报错。

#### Function Explanation

- `ElMessageBox.confirm`：Element Plus 的确认弹窗 Promise 接口。确认就继续执行，取消会 reject，所以前端最省事的写法就是 `try/catch` 收掉取消分支。
- `sqlite3_changes`：返回最近一条写语句影响的行数。这里删 summary 后用它判断“这次事务到底有没有删到目标主记录”。

#### Pitfalls

- 仓储层已经拿着互斥锁时，不能再直接调用也会拿同一把锁的高层查询函数，否则就是自锁死。这次所以额外拆了 `HasTraceSummaryLocked`。
- 浏览器发 `DELETE` 通常会先走 `OPTIONS` 预检；后端如果只记得加业务路由，不补 `Access-Control-Allow-Methods: DELETE`，页面上按钮会像“没反应”一样失败。
- 删除按钮如果不 `stop` 冒泡，用户点“删除”时会先触发行点击，把详情抽屉打开，然后再弹确认框，体验会非常别扭。
