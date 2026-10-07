# Monorepo 结构与开发约定

2026-10-07 按用户要求，将同一 Git 仓库中的网页、原生仿真和 Windows 图形入口按应用组织，将共享游戏逻辑和 Wasm 桥接按包组织。当前玩法仍为配置 v6、ABI v5；迁移不调整费用、弹形、GPA、难度或按住瞄准行为。既有 [Pages v6](https://cheng-xiu.github.io/ustc-danmaku/) 可以继续试玩，源码目录迁移与线上重新发布分别登记。

## 目录与依赖方向

```text
ustc-danmaku/
├─ apps/
│  ├─ web/                 PixiJS + TypeScript 网页、菜单、输入与渲染
│  ├─ sim/                 原生无界面回放与日志入口
│  └─ desktop/             原 Windows/EasyX 图形入口、platform/ 与 render/
├─ packages/
│  ├─ core/                共享 C 核心
│  │  ├─ core/             物理、技能、能量、无尽规则与计分
│  │  ├─ ai/               当前学生脚本
│  │  ├─ sources.txt       唯一完整 C/AI 源文件清单
│  │  └─ CMakeLists.txt
│  └─ wasm/                C/Wasm 薄桥接、ABI 导出与桥接探针
├─ tests/                  C 回归、Native/Wasm 对照与浏览器检查
├─ scripts/                统一构建、打包、服务及 Pages 发布脚本
├─ docs/                   当前文档与保留身份的历史证据
├─ references/             原 PDF 等参考资料
├─ demo/                   已提交并验证的单文件交付物
├─ CMakeLists.txt          原生项目与测试总入口
├─ package.json            npm workspace 与统一命令总入口
└─ package-lock.json       唯一 npm 依赖锁
```

`packages/core` 提供同一个 `demo_core` C 库；`apps/sim` 与 `apps/desktop` 链接它，`packages/wasm` 将它通过桥接导出给 `apps/web`。Web 的 TypeScript 处理输入、固定步调度和只读显示，不复制碰撞、技能几何、AI、随机数、能量或计分。Wasm 桥接依赖核心，核心不依赖 Web、Wasm、EasyX 或平台输入。

这里的共享包按源码职责划分，不代表发布到 npm。当前唯一 JavaScript workspace 为 `apps/web`，名称 `@ustc-danmaku/web`；C 应用和包使用 CMake。没有新增训练应用、模型服务或通用构建框架。

## 根目录统一入口

需要 Node 22.12+、npm 10+。当前锁与实际工具记录为 npm 11.17.0；PixiJS 8.22.0、TypeScript 5.9.3、Vite 7.1.12 保持锁定。Wasm SDK 固定 Emscripten 6.0.11；原生构建需 CMake 3.16+ 和 C/C++ 编译器，Windows 可使用现有 MinGW。浏览器验收需本机 Chrome。

在仓库根安装一次，使用唯一锁文件和根 `node_modules/`：

```powershell
npm.cmd ci
npm.cmd run setup:wasm
npm.cmd run build
npm.cmd test
```

已有相同 SDK 时可跳过 `setup:wasm`。当前根 npm 脚本通过 PowerShell 执行 SDK/原生相关步骤：Windows 调用 `powershell.exe`，其他平台需要 `pwsh` 和相应编译工具；未验证的平台不能仅依据脚本存在宣称构建通过。

| 根命令 | 职责或产物 | 前置条件 |
| --- | --- | --- |
| `npm run setup:wasm` | 安装/激活固定 SDK | 需要时执行一次 |
| `npm run dev` | 启动 `apps/web` 的 Vite 开发服务器 | 安装依赖并先构建 Wasm |
| `npm run preview -- --port 4173` | 预览网页生产输出 | `build:web` 已完成 |
| `npm run check` | TypeScript 严格类型检查 | 根依赖已安装 |
| `npm run build:native` | 原生目标构建与 CTest | CMake、编译器 |
| `npm run build:wasm` | 生成 `apps/web/public/wasm/demo-core.mjs` 与 `.wasm` | 固定 SDK |
| `npm run build:web` | 类型检查并生成 `apps/web/dist/` | 已生成当前 Wasm |
| `npm run build:standalone` | 生成 `build/release/ustc-danmaku-endless.html` | 当前 Wasm 和根依赖 |
| `npm run build` | 按 Wasm → Web → 单文件顺序完整构建 | SDK、根依赖 |
| `npm run test:input` | 输入、owner、按住/松手/取消检查 | 根依赖 |
| `npm run test:core` | Native/Wasm 逐 tick 对照 | 原生与 Wasm 构建完成 |
| `npm run test:browser` | 最终构建单文件的真实浏览器检查 | 构建单文件、Chrome |
| `npm run test:endless` | 最终构建单文件的手动清波/下一波回放 | 构建单文件、Chrome |
| `npm run test:monorepo` | 开发/预览/仓库/试玩包 HTTP 启动，端口透传与真实发招 | 完整构建、Chrome；每次创建新包目录 |
| `npm test` | 完整构建 → 原生 → 输入 → 对照 → 浏览器 → 无尽 | 完整工具链 |
| `npm run package:web -- -OutputRoot build/release-v6` | 可选静态包与 ZIP | 完成构建及验收，选择不存在的包路径 |
| `npm run package:source -- -OutDir build/release-monorepo-source` | 源码与已有原生产物、逐文件哈希 | 选择不存在的输出路径；已有二进制记录为未核验来源 |

复跑单项检查时须保证其输入产物与当前源码对应；不能改了 C 只重建 TS，就把旧 Wasm 的通过当成新核心通过。`npm test` 遇到失败停止，不覆盖历史验收结论。

## C 核心的单一清单

`packages/core/sources.txt` 列出当前 16 个纯 C/AI 源文件，路径相对 `packages/core/`。`packages/core/CMakeLists.txt`、Wasm 构建与打包脚本读取这一个清单。增加核心文件时先更新它，再检查原生/Wasm 两条路径；不要从根 `CMakeLists.txt` 抽取或在各脚本维护第二套完整列表。

少数窄单元测试自带桩而仅链接指定核心模块，这是隔离测试的依赖，不是另一套游戏核心。原生图形的 C++/EasyX 源码位于 `apps/desktop`，不进入共享 C 或 Wasm 清单。核心行为修改仍集中到同一配置和 C 实现，并按当前规则更新版本与验证证据。

## 旧路径映射与历史证据

| 迁移前 | 当前位置 |
| --- | --- |
| `web/` | `apps/web/` |
| `sim/` | `apps/sim/` |
| `game_main.cpp` | `apps/desktop/game_main.cpp` |
| `platform/` | `apps/desktop/platform/` |
| `render/` | `apps/desktop/render/` |
| `core/` | `packages/core/core/` |
| `ai/` | `packages/core/ai/` |
| `wasm/` | `packages/wasm/` |
| `web/package-lock.json` | 根 `package-lock.json`，由 workspace 安装维护 |

`tests/`、`scripts/`、`demo/`、`docs/` 和 `references/` 保持根目录位置。旧版验证报告、归档文档、原始 JSON/日志包含其当时的命令、源码路径、提交和产物 hash，保留原内容；阅读它们时使用此映射定位现版源码。原规范 PDF 不修改，旧报告不能凭路径调整继承为本次迁移已通过。

本轮根构建、原生 CTest13/13、输入54例/431断言、Native/Wasm36,010快照、离线浏览器282检查及无尽回放18检查均已通过；结果与原始证据见 [迁移复验](monorepo-validation.md)。重建 HTML 与现有发布物字节一致。v6 既有玩法证据见 [瞄准登记](validation/aim-v6.md) 与 [工程验证](web-demo-validation.md)，这些记录对应迁移前修订。

## 交付、发布和并行协作

常规构建生成被忽略的 `build/release/ustc-danmaku-endless.html`，不会自动覆盖已提交的 `demo/ustc-danmaku.html`。完成验收后，由母代理同步并提交交付物；发布仍运行根 `node scripts/publish-pages.mjs`，读取已提交的 `demo/ustc-danmaku.html`，核对源码修订和字节，再更新 `gh-pages`。具体约束见 [Pages 指南](github-pages.md)。仅移动目录不代表线上已经更新。

公共 C 头、ABI、源清单、配置、根入口、依赖与锁文件由母代理统一维护；各子任务按 `apps/*`、`packages/*` 的实际文件范围分派，一个文件一个写入者。代理模型、深度和 Git 写操作限制继续遵守 [AGENTS](../AGENTS.md) 与 [母代理提示词](web-demo-agent-prompt.md)。

后续机器学习训练仍应复用原生 C 核心；`apps/sim` 当前是脚本回放与日志入口。monorepo 只明确共享边界，观察、动作、奖励、决策节拍、批量环境及模型导入未因目录迁移自动完成。
