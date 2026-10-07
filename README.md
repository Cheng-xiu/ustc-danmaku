# 科大弹幕录：绩点保卫战

计算机程序设计 C* 课程项目。当前网页 Demo 方案为 **PixiJS + TypeScript + C/WebAssembly**；游戏规则继续由 C99/C11 核心实现，同一核心的原生无界面构建用于回归与后续离线训练。

## 先试玩

**网页版最小可玩 Demo 已实现。** 玩家控制 Boss，学生使用脚本躲弹并反击；包含四招、共享能量、真实命中与胜负、暂停和重开。默认规则为配置 v2、种子 20261006、3 名学生。

收到预构建试玩 ZIP 后解压，双击 `Start-Web-Demo.bat`；需要 Node.js 22.12+，无需 Emscripten、EasyX 或编译器。浏览器由本地 HTTP 服务打开，关闭服务窗口即停止。不能直接双击 HTML。

源码构建：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup-web-toolchain.ps1
powershell -ExecutionPolicy Bypass -File scripts/build-wasm.ps1
Set-Location web
npm.cmd ci
npm.cmd run build
Set-Location ..
.\Start-Web-Demo.bat
```

鼠标朝指针移动；WASD/方向键备用；1–4 出招；Esc/右键暂停；R 重开。获胜条件是在自己血量耗尽前击倒全部学生。

- [运行指南](docs/web-demo-guide.md)：构建、试玩、静态部署与排错。
- [工程验证](docs/web-demo-validation.md)：原生 8 项测试、10,803 份原生/Wasm 快照对照、39 项浏览器检查及性能采样。
- [玩法发现](docs/web-playability-findings.md)：真实默认胜局与设计风险。当前恢复速度使每招结束后都能再次支付全部招式，蓄能取舍较弱；未擅自调参。
- [真人试玩记录模板](docs/web-demo-playtest.md)：真人易懂程度、难度与四招用途仍待验证，机器学习尚未训练。

## 当前规划

主玩法调整为：**玩家操作 Boss，朝鼠标指针移动、主动选择招式；脚本 AI 学生躲弹并反击。** 通过共享能量及不同用途的小招、大招，让玩家权衡立即进攻与留能量等待机会。

- [Boss 模式：基本玩法与实施建议](docs/boss-mode-plan.md)：当前核心规则、实施顺序、检查依据及相对原方案的变化。
- [机器学习辅助平衡：实施建议](docs/ai-balance-plan.md)：离线 AI 寻找可疑强招、开发者调整、重新训练与真人验证的执行流程。
- [PixiJS 网页 Demo 实施规划](docs/web-demo-plan.md)：已批准的技术方案、迁移基线、C/Wasm 桥接、固定步长、12 个小任务及验收门槛。
- [网页 Demo 母代理执行提示词](docs/web-demo-agent-prompt.md)：可全文交给母代理；保留指定模型与母→子→孙层级限制。

以上规划记录用户后续明确的修改。网页技术方案已批准；Demo 的同步死亡、反击方向、时限等已批准记录见网页规划。尚未明确的交互和试验数值需结合基线核对，不能当作已经验证的结论。

## 完整原始规范

[最终项目规划与交付规范 v3.1（Markdown）](docs/project-spec-v3.1.md) 保留原方案全文，是理解原始规则和技术约束的依据；它描述的学生主视角、固定选招窗口等已调整内容，须结合当前 Boss 规划阅读。

`docs` 使用 Markdown，完整保留原规范的章节、表格、公式、命令和示意图，不以精简版本替代。

[PDF 原文件](references/project-spec-v3.1.pdf)：`科大弹幕录_最终交付文档_v3.1.pdf`（2026-10-05，共 23 页）。原始字节保留供核对；若转写与原 PDF 不一致，以原 PDF 为准。

后续工作先阅读完整规范和当前规划；未被调整的内容仍以原 PDF 为依据，转写有疑问时核对原 PDF。文档未规定或不清楚的关键问题向项目用户确认。

## 当前状态

本分支以用户提供的 Windows/EasyX Demo 源码提交 `753a9f6` 为迁移基线，保留历史前端与记录，新增共享 C 核心的网页入口。工程验证已执行；真人玩法验收、其他设备/浏览器测试及机器学习训练待开展，尚未发布到公共网站。详见本轮验证报告，不继承历史报告的通过声明。

游戏与无图形测试／训练器仍规划共用纯 C 逻辑核心。先完成基本玩法，随后用离线 AI 辅助平衡；新能量玩法的状态、动作和模型需重新定义，不能直接套用原情境老虎机及快照。

## 协作

自动化开发代理应遵循 [AGENTS.md](AGENTS.md)。
