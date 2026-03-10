# 20260310 feat ai-proxy-concurrency

Git Commit Message
feat(ai-proxy): 使用线程池桥接同步 provider 调用

Modification
- server/ai/proxy/main.py
- docs/todo-list/Todo_TraceAiProvider.md
- docs/dev-log/20260310-feat-ai-proxy-concurrency.md

Learning Tips
Newbie Tips
- `async def` 不是魔法。既然路由函数里还是直接调用同步阻塞函数，那么 event loop 一样会被卡死。外面写成 async，只代表“这里允许 await”，不代表内部同步代码会自动异步。
- 这次没有去改 `AIProvider` 基类和所有 provider 的函数签名，是因为当前真正的瓶颈在路由层。既然 `main.py` 已经是 async 路由，那么先把同步 provider 丢进线程池，就能先把 event loop 保护住；如果一上来把整套 provider 抽象都改成 async，会把 mock、gemini、基类接口一起卷进去，改动面会一下子膨胀。

Function Explanation
- `starlette.concurrency.run_in_threadpool`
  这是框架给 async 路由准备的桥接函数。既然当前 provider 还是同步 `def`，那就不能在 event loop 线程里直接跑。这里把同步函数丢到后台线程执行，外层继续 `await` 结果，event loop 自己就能去服务别的请求。
- `await request.body()`
  这是异步读取 HTTP body。既然 body 可能还在底层网络缓冲区里，那么读取动作本来就可能等待，所以这里应该是 await，而不是同步硬读。

Pitfalls
- 不要误以为“把同步函数名字改成 `async def`”就异步了。既然函数内部还是同步 SDK 调用、还是 `time.sleep`，那只是换皮，event loop 还是会被堵。
- 不要在 event loop 线程里直接跑第三方同步 SDK。既然像 Gemini 这种调用本质上还是同步网络请求，那么如果不做线程池桥接，单个慢请求就会把整个 Python proxy 的接入层一起拖慢。
- 线程池桥接解决的是“别堵住 event loop”，不是“全局并发策略已经设计好了”。如果后面要做模型降级、限流、优先级或者 fallback，这些还得在更外层单独控制，不能指望线程池本身替你兜底。
