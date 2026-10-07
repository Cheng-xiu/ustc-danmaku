# 最小可玩 Demo 提示词入口

更新：2026-10-07。当前阶段为 **PixiJS + TypeScript + C/Wasm 网页迁移**，用户已批准技术方案。

请把 [网页 Demo 母代理执行提示词](web-demo-agent-prompt.md) 全文交给母代理；技术分工、W01–W12 小任务和验收门槛见 [网页 Demo 规划](web-demo-plan.md)。

模型要求保持：母代理 `deepseek账号/deepseek-flash`；子代理和孙代理 `a6api/deepseek-v4.1-flash`。最多母→子→孙，孙代理不得继续派生。

[2026-10-06 Windows/EasyX 原提示词](archive/minimal-demo-agent-prompt-windows-2026-10-06.md) 完整保留为历史依据。其中从零开发、S01–S20 和 Windows 前端要求不作为当前网页阶段的执行指令。已有 Demo 源码和调试记录用于核对迁移基线，历史通过项不等于网页已验收。
