# 项目规划与文档索引

## 本轮可玩网页交付

当前为 **monorepo、配置 v6 / Web ABI v5** 的手动瞄准无尽网页 Demo，[直接试玩文件](../demo/ustc-danmaku.html) 和 [GitHub Pages](https://cheng-xiu.github.io/ustc-danmaku/) 均为单文件游戏。按住 1–4 或按钮预瞄、松手释放、Space 取消；持有时仅实际鼠标移动更新方向，角色走位不自行转向。顶部能量/CD、开局介绍、窗口比例战场、圆形校徽、第4招100、逐波3→8及击倒数收敛 GPA 均保留。

- [monorepo.md](monorepo.md)：`apps/` 与 `packages/` 职责、唯一根 npm 锁、16 文件共享 C 清单、统一构建及旧路径映射。
- [monorepo-validation.md](monorepo-validation.md)：目录迁移后的根入口、13 项原生测试、输入、36,010 快照对照、282 项离线交互和启动/打包实测。
- [repository-sync.md](repository-sync.md)：手机适配前的 monorepo 主线同步记录；当前触控交付与验证见手机报告。
- [web-demo-guide.md](web-demo-guide.md)：试玩、构建与部署。
- [mobile-controls.md](mobile-controls.md)：手机自动识别、浮动摇杆、右侧选招转盘与多指浏览器验证。
- [demo-rules.md](demo-rules.md)：当前唯一规则与集中参数。
- [validation/aim-v6.md](validation/aim-v6.md)、[web-demo-validation.md](web-demo-validation.md)：v6 手动方向、释放事务、最终 HTML 与真实浏览器证据，保留所测修订身份。
- [web-playability-findings.md](web-playability-findings.md)：当前无尽循环的游戏性评估与建议。
- [web-demo-playtest.md](web-demo-playtest.md)：真人试玩记录模板，当前尚无真人结论。
- [web-abi.md](web-abi.md)、[web-toolchain.md](web-toolchain.md)：已实现 ABI 与固定工具链。

开发从仓库根执行 `npm ci`、`npm run build` 和 `npm test`；不要在 `apps/web` 另行安装或建立锁文件。当前 16 个纯 C/AI 文件由 `packages/core/sources.txt` 唯一列出，原生仿真和 Wasm 使用同一实现。

本轮由 Codex 及其子代理完成；指定 DeepSeek 模型提示词保留供后续外部执行，不据此声称本轮使用过该模型。

## 当前实施规划

- [boss-mode-plan.md](boss-mode-plan.md)：人类 Boss、鼠标指针移动、主动出招、共享能量、短期与长期目标取舍，以及基本玩法检查依据。
- [ai-balance-plan.md](ai-balance-plan.md)：在基本玩法稳定后，用离线 AI 发现策略漏洞、调参、重新训练及独立验证。
- [web-demo-plan.md](web-demo-plan.md)：PixiJS + TypeScript + C/Wasm 网页迁移方案与任务状态；实现和基础工程验证已完成，真人及跨设备验收待开展。

以上规划记录用户对原方案的后续调整。网页已复用用户提供的 Windows/EasyX Demo 基线；当前进入真人试玩阶段，机器学习尚未训练。涉及主控角色、选招节奏、能量、网页技术和新模式训练等内容时，以当前获准规划及 Demo 规则记录为准；其余内容仍须通读并核对完整原规范。

## 增量实施提示词

[web-demo-agent-prompt.md](web-demo-agent-prompt.md) 是当前执行入口，可全文交给实施母代理，包含当前 monorepo 基线、M00 接口冻结、实际模块、增量小任务、子/孙模板和验收。现有网页与桥接已完成，按受影响范围派任务，不从零重写。母代理模型为 `deepseek账号/deepseek-flash`，子代理和孙代理为 `a6api/deepseek-v4.1-flash`；孙代理不得继续派生。

[minimal-demo-agent-prompt.md](minimal-demo-agent-prompt.md) 保留为兼容入口，指向当前网页提示词；[原 Windows 提示词](archive/minimal-demo-agent-prompt-windows-2026-10-06.md) 完整保留为历史依据。旧 S01–S20 任务表及报告不自动转成网页任务或通过证据。

提示词是外部执行说明；网页实现与工程结果以本轮运行指南、验收报告为准，模型尚未训练、真人体验尚未验证。试验数值集中于同一 C 配置，网页前端不维护另一套规则。

## 历史实现与验收记录

以下保留对应阶段的正文、命令、旧路径、版本、修订和结论，不作为现版执行入口；旧路径按 [monorepo 映射](monorepo.md#旧路径映射与历史证据) 定位现版源码，历史通过数不自动继承。

- [demo-guide.md](demo-guide.md)、[demo-interfaces.md](demo-interfaces.md)、[demo-tasks.md](demo-tasks.md)：2026-10-06 Windows/EasyX 有限局原型的交付、接口冻结和任务快照。接口文档首段的 cfg4/ABI3 也是后续旧修订注记；现版 ABI 以 `web-abi.md` 的 v5 为准，不按旧“当前”字样恢复规则。
- [demo-validation.md](demo-validation.md)、[demo-findings.md](demo-findings.md)：早期 Windows 验收、失败与修订指纹，保留当时的未执行项与发现。
- [demo-playtest.md](demo-playtest.md)：早期有限局真人试玩空白模板；现版使用 `web-demo-playtest.md`，不把旧胜利目标或配置 v1 重新用于无尽产品。
- [web-core-fixes.md](web-core-fixes.md)：初始网页迁移的 C 基线修复与回归记录。
- [web-balance-findings.md](web-balance-findings.md)、[web-balance-findings-v5.md](web-balance-findings-v5.md)：v4/v5 自动目标几何的实际平衡实验；v5 的 12 种子、1,152 局主要对照不能证明 v6 手动方向已平衡。
- [archive/](archive/) 与 [validation/](validation/)：原版规划、提示词、报告及分版原始证据。当前 v6 与 monorepo 报告也按实际受测修订保存，不为同步主线改写旧 hash 或检查数。

## 完整原始规范

[project-spec-v3.1.md](project-spec-v3.1.md) 为《科大弹幕录：绩点保卫战》完整项目规划与交付规范的 Markdown 转写，保留全部章节、表格、公式、训练命令与 6 张原图。图像放在 `assets`，供 Markdown 引用。

[原始 PDF](../references/project-spec-v3.1.pdf) 保留供核对。

- 原文件：`科大弹幕录_最终交付文档_v3.1.pdf`
- 版本：v3.1，2026-10-05，共 23 页
- SHA-256：`1668d0d3d62a8fa991f6822b85d101925ef434bb8c139370abcace2f6b3b4f7a`

原始 PDF 与 Markdown 转写正文均保留不变。若转写与原文不一致，以原 PDF 为准；用户后续明确指令及当前规划中的获准变化优先于原方案。不明确的关键问题先询问用户。
