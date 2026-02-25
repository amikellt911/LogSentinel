# 20260225 feat-skill-daily-cpp-bagua

## Git Commit Message
`feat(skill): 新增每日随机C++后端校招八股教学技能`

## Modification
- `docs/skills/daily-cpp-backend-bagua/SKILL.md`
- `docs/skills/daily-cpp-backend-bagua/agents/openai.yaml`
- `docs/skills/daily-cpp-backend-bagua/references/bagua-topic-pool.md`
- `docs/todo-list/Todo_DailyCppBaguaSkill.md`

## Learning Tips

### Newbie Tips
- 八股学习不要并行展开多个题目，一次吃透一个点更容易形成可复述的长期记忆。
- 面试回答不要只背结论，要同时准备“为什么成立”和“什么情况下不成立”。

### Function Explanation
- `init_skill.py`：创建标准技能目录、`SKILL.md` 模板与 `agents/openai.yaml` 基础结构。
- `generate_openai_yaml.py`：生成技能 UI 元数据，并校验 `short_description` 等约束。
- `quick_validate.py`：校验技能 frontmatter 和目录结构，确保技能可被识别与加载。

### Pitfalls
- 若缺少“没懂后的降复杂度分支”，模型容易重复原解释，学习效率会持续下降。
- 若没有“复述通过后再微坑点”的流程，容易出现熟悉感陷阱：看起来懂但不能稳定表达。
- 若不限制“一次只讲一个”，输出会退化成刷题清单，用户难以形成真正掌握。
