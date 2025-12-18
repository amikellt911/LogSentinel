# HTTP 接入层模块文档

## 1. 本模块职责

**概括**:实现 LogSentinel 的 简单线程池
相关文件:
threadpool/ThreadPool.h,threadpool/ThreadPool.cpp,base_performance_results_without_log_between_threadpool.log,
threadpool_performance_results_without_log.log,
tests/threadpool_test.cpp,
src/testserverpool.cpp.

## 2. 核心流程图和数据流

![核心流程图](image.png)

### 核心组件

- 


## 3. 关键决策

### 3.1 





## 4. 测试

### 4.1 单元测试

通过 `tests/threadpool_test.cpp` (gtest) 测试 ThreadPool,覆盖了以下场景:

- 正常情况
- 关闭但是任务队列还有任务
- 关闭后提交任务

### 4.2 性能测试

通过 `performance_test.sh` 进行性能测试,测试结果:

| 测试场景 | QPS | 日志文件 |
|---------|-----|---------|
| 有业务内容（用sleep模拟） + 同步无线程池架构| ~35k | `base_performance_results_without_log_between_threadpool.log` |
| 无业务内容 + 同步无线程池架构 | ~98k | `base_performance_results_without_log.log` |
|  有业务内容（用sleep模拟） + 异步线程池架构 | ~50k | `threadpool_performance_results_without_log.log.log` |

> **说明**: 异步的作用。

## 5. 总结

下一步是saga模式和进一步优化线程池的背压机制（降级，熔断，拒绝），因为生产者io线程的生产速度远超于消费者消费速度，堰塞湖，需要优化，最好让每个work线程都持有一个sqlite3的thread_local句柄减少锁开销