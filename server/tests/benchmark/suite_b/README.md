# Suite B 目录说明

这里预留给生命周期鲁棒性实验。

后续正式实现时，相关资产统一放在这里：

- `sender.py`
- `profiles.py`
- `evaluator.py`
- `run_suite_b.py`
- `manifest/` 或等价输出目录

这条线故意不复用 `common/wrk/` 当主入口。

原因：

- Suite B 关注的是乱序、晚到、replay 的真实语义；
- 它需要 sender manifest 和 evaluator，不只是高吞吐 wrk 压流。
