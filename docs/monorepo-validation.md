# Monorepo 迁移复验

本轮将网页、原生仿真及 Windows 入口整理到 `apps/`，唯一 C/AI 实现与 Wasm 桥接整理到 `packages/`。根 npm workspace、单一依赖锁和共享 16 源文件清单均已接入实际构建；目录说明见 [monorepo 指南](monorepo.md)。

迁移基线为 `f4bdd9dbb67440c49f21eb64459f1259f69933b0`。本报告与原始证据对应包含本报告的迁移提交，cfg6 / ABI5 保持原值；历史 v6 报告保留其原路径和修订身份。

## 已通过的根入口复验

在 Windows、Node 24.19.0、npm 11.17.0、GCC 8.1.0、CMake 4.4.4、Emscripten 6.0.11 和 Chrome 154.0.8037.98 下执行：

```powershell
npm.cmd ci --offline --audit=false --fund=false
npm.cmd test
```

`npm test` 退出码 0，顺序完成 Wasm → TypeScript/Vite → 单文件 → 原生构建/CTest → 输入夹具 → Native/Wasm 对照 → 离线浏览器 → 无尽回放。未用旧提交的 HTML 替代本轮构建输出。完整日志见 [root test](validation/monorepo/monorepo-root-test.log)。

| 验证 | 本轮结果 | 原始证据 |
| --- | --- | --- |
| 原生 CTest | 13/13 通过 | 根测试日志 |
| 输入状态机 | 54 例、431 断言通过 | [输入报告](validation/monorepo/aim-input-probe-results.json) |
| Native/Wasm | 10 场景、36,010 快照；离散量一致，浮点最大差 0.000003814697265625，小于 0.002 容差 | [逐 tick 对照](validation/monorepo/core-parity.json) |
| 最终单文件浏览器 | 282 检查通过；15 次真实发招记录；无脚本错误、无外部资源请求 | [离线交互](validation/monorepo/v6-aim-browser-smoke.json) |
| 无尽手动回放 | 18 检查通过；tick866 清波、tick986 下一波出生、tick1016 Boss HP3 | [回放](validation/monorepo/v6-browser-endless.json) |
| 独立 Wasm 包原生模式 | 桥接 CTest1/1，四招预瞄纯度、锁定 rays 与实际释放一致 | [桥接日志](validation/monorepo/wasm-package-native-ctest.log) |
| Windows/EasyX | MSVC/CMake 图形目标编译链接成功 | [构建日志](validation/monorepo/msvc-cmake-build.log) |

Windows 图形入口在本次只验证构建，没有打开游戏窗口；网页交互验收使用真实 Chrome。输入夹具和自动回放分别属于状态机测试和公开输入脚本，不能写成真人试玩或强化学习成果。未验证 Linux/macOS 或其他浏览器的构建与性能。

## 源码、依赖与交付物完整性

[完整性报告](validation/monorepo/monorepo-final-integrity.json) 对照迁移前记录，确认 62 个原生源码/参考文件字节一致，以及 17 个网页源码/配置文件的 Git blob 一致。共享规则、学生 AI、计分、手动预瞄与发射逻辑均未重写。81 个锁定第三方条目的版本保持一致。根锁文件为唯一 npm 锁，唯一 JavaScript workspace 为 `apps/web`。

本轮生成单文件大小仍为 1,168,141 bytes，与已提交且已发布的 v6 试玩 HTML 字节一致：

- HTML SHA256：`9de27925103b644236b736e635932be215ecd0440d095311657c08673d22680d`
- Wasm SHA256：`9dbd096c2eac3792de2c22b5af54725591fd99502f6239b16a41257a13c21b93`

用户原先工作目录中的 `tests/test_attack.c` 与原规范 PDF 也核对为原哈希。已有 Demo 调试记录、原 PDF、历史交付物及报告不覆盖。

## 启动与打包

运行 `node tests/web/monorepo-smoke.mjs`（根 `npm run test:monorepo` 调用的同一脚本），HTTP 与试玩包验收 66/66 通过；完整证据见 [HTTP/打包报告](validation/monorepo/monorepo-http-smoke.json)。每次复跑会创建新的包目录，需要先完成当前源码构建。

| 入口 | 实测 | 浏览器资源数 |
| --- | --- | --- |
| 根 `npm run dev -- --port 5187 --strictPort` | 自定义端口透传正确，Vite 开发模式运行 | 38 |
| 根 `npm run preview -- --port 5188 --strictPort` | 自定义端口透传正确，生产预览运行 | 14 |
| 仓库 `node scripts/serve-web.mjs` | 默认读取 `apps/web/dist` | 14 |
| 试玩包 `node scripts/serve-web.mjs` | 默认读取包内 `web/dist` | 14 |

四入口均真实开始游戏、按住技能2预瞄、移动6tick验证方向稳定、松手只接受一次并扣50能量；48候选与48条锁定预警一致，tick68实际发出24个首波弹，Space取消没有补发。每个入口的运行、资源请求及HTTP错误均为0，测试子进程全部关闭。原启动器包含仓库和下载包两种目录分支；本次直接验证了其服务器路径，没有弹出交互批处理窗口。

本次试玩包 manifest 的149个文件哈希全部匹配，12项生产依赖许可证完整，ZIP大小3,460,559bytes，HTML仍为原字节。根命令的端口参数透传和无profile子进程的哈希计算均已修复并经上述测试验证；哈希计算使用.NET，不依赖额外PowerShell模块。

另实际执行 `npm run package:source -- -OutDir build/monorepo-source-package-final`，导出264个源码快照文件；唯一锁位于根目录，缓存、构建目录、node_modules和Git元数据未混入，所有manifest源码/已有二进制哈希匹配，既有输出拒绝覆盖且manifest不变。此目录是本轮打包验证快照，包含的HEAD及dirty标识按生成时记录。源码包的已有原生二进制额外记录来源与哈希、标为 `existing-unverified`；打包过程不证明它们对应当前源码。两种打包脚本均拒绝覆盖既有输出。

本轮生成的游戏文件与现有 Pages 完全一致，因此本次提交只同步源码组织和工具入口，无需重新发布相同网页。
