# 20260222 feat-skill-deep-learning-mode

## Git Commit Message
`feat(skill): 新增深度学习模式并支持末尾-xx触发`

## Modification
- `docs/skills/deep-learning-mode-xx/SKILL.md`
- `docs/skills/deep-learning-mode-xx/agents/openai.yaml`
- `docs/todo-list/Todo_DeepLearningMode.md`

## Learning Tips

### Newbie Tips
- 技能的触发条件应优先写进 `SKILL.md` 的 `description`，因为这是模型是否加载该技能的首要依据。
- 规则类技能先写“触发/退出”，再写“执行模板”，可以避免后续行为漂移。

### Function Explanation
- `init_skill.py`：快速初始化技能目录和 `SKILL.md` 模板，适合新建技能时统一骨架。
- `generate_openai_yaml.py`：根据技能内容生成 `agents/openai.yaml`，并校验 UI 字段约束（如 `short_description` 长度）。
- `quick_validate.py`：检查技能 frontmatter、命名和结构合法性，确保技能可被稳定识别。

### Pitfalls
- `short_description` 长度不足会导致 `openai.yaml` 生成失败，需控制在 25-64 字符。
- 若把“何时触发”只写在正文而非 frontmatter，会增加技能无法被自动触发的风险。
- 用户说“懂了”后若不强制进入检验环节，很容易出现“熟悉感陷阱”，看懂但不会用。

## 追加记录（迭代）

### Git Commit Message
`refactor(skill): 优化深度学习模式的没懂分支与复述后设坑流程`

### Modification
- `docs/skills/deep-learning-mode-xx/SKILL.md`
- `docs/todo-list/Todo_DeepLearningMode.md`
- `docs/dev-log/20260222-feat-skill-deep-learning-mode.md`

### Learning Tips

#### Newbie Tips
- 教学型技能要把“听不懂时怎么办”写成明确分支，否则模型容易重复原解释而不降复杂度。
- “我懂了”不等于“会用了”，先复述再设坑能显著降低假理解。

#### Function Explanation
- 复述校验：通过让用户重建因果链，验证其是否掌握前提、过程和边界，而不是只记住结论。
- 先验补齐：先补最小前置知识点，再回到主问题，能避免解释不断分叉导致认知负担上升。

#### Pitfalls
- 用户反馈“没懂”时若直接重复原答案，通常只会放大挫败感，需要主动自检并缩短因果链。
- 过早出坑点会把“理解验证”变成“答题压力”，应在复述通过后再进入坑点检验。

## 追加记录（依赖进程自动拉起）

### Git Commit Message
`feat(server): 增加开发模式依赖进程自动拉起能力`

### Modification
- `server/util/DevSubprocessManager.h`
- `server/util/DevSubprocessManager.cpp`
- `server/src/main.cpp`
- `server/CMakeLists.txt`
- `docs/todo-list/Todo_RuntimeDeps_Autostart.md`
- `docs/dev-log/20260222-feat-skill-deep-learning-mode.md`

### Learning Tips

#### Newbie Tips
- 在 C++ 里拉起外部服务时，优先用 `fork + exec` 而不是 `system("... &")`，因为前者能拿到 PID 并做可控回收。
- 自动拉起依赖服务时要先做端口探测，避免重复启动导致端口冲突。
- 主进程退出前需要回收子进程，否则容易残留后台进程影响下一次联调。

#### Function Explanation
- `fork()`：复制当前进程，父进程拿到子 PID，子进程返回 0。
- `execlp()`：在子进程中替换为目标程序，成功后不会返回。
- `waitpid(..., WNOHANG)`：非阻塞检查子进程是否已退出，用于启动等待和析构回收。
- `kill(pid, SIGTERM/SIGKILL)`：先温和终止，再强制终止，确保进程可被清理。

#### Pitfalls
- 只启动子进程但不做就绪探测，会导致主服务比依赖服务先对外提供能力，联调时出现随机失败。
- 子进程启动失败若不快速返回，主服务会在“依赖不可用”状态下继续运行，问题更隐蔽。
- 只发 `SIGTERM` 不兜底 `SIGKILL`，可能在子进程卡死时造成长期残留。
