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
