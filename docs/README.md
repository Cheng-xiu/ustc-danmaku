# 项目规划与文档索引

## 当前实施规划

- [boss-mode-plan.md](boss-mode-plan.md)：人类 Boss、摇杆移动、主动出招、共享能量、短期与长期目标取舍，以及可执行的基本玩法任务。
- [ai-balance-plan.md](ai-balance-plan.md)：在基本玩法稳定后，用离线 AI 发现策略漏洞、调参、重新训练及独立验证。

两份规划记录用户对原方案的后续调整，状态均为待实施。涉及主控角色、选招节奏、能量及新模式训练等内容时，以当前规划为准；其余内容仍须通读并核对完整原规范。

## 最小可玩 Demo 实施提示词

[minimal-demo-agent-prompt.md](minimal-demo-agent-prompt.md) 可全文交给实施母代理，包含最小 Boss demo 范围、待确认规则、20 个小任务、子代理与孙代理通用提示词、依赖波次及验收。母代理模型为 `deepseek账号/deepseek-flash`，子代理和孙代理为 `a6api/deepseek-v4.1-flash`；孙代理不得继续派生。

该提示词是实施任务说明，不代表游戏已经开发、模型已经训练或玩法已经验证。其中建议的规则须按仓库协作要求确认，试验数值集中配置。

## 完整原始规范

[project-spec-v3.1.md](project-spec-v3.1.md) 为《科大弹幕录：绩点保卫战》完整项目规划与交付规范的 Markdown 转写，保留全部章节、表格、公式、训练命令与 6 张原图。图像放在 `assets`，供 Markdown 引用。

[原始 PDF](../references/project-spec-v3.1.pdf) 保留供核对。

- 原文件：`科大弹幕录_最终交付文档_v3.1.pdf`
- 版本：v3.1，2026-10-05，共 23 页
- SHA-256：`1668d0d3d62a8fa991f6822b85d101925ef434bb8c139370abcace2f6b3b4f7a`

原始 PDF 与 Markdown 转写正文均保留不变。若转写与原文不一致，以原 PDF 为准；用户后续明确指令及当前规划中的获准变化优先于原方案。不明确的关键问题先询问用户。
