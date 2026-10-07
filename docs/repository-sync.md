# 最新版仓库同步说明

本轮按用户 2026-10-08 的要求，把仓库源码、现行文档、构建/打包入口及线上试玩对应到同一最新版。当前产品为 **配置 v6 / Web ABI v5**，采用 PixiJS + TypeScript + 共享 C/Wasm 的 monorepo；版本与目录迁移后的实测见 [monorepo 指南](monorepo.md) 和 [迁移复验](monorepo-validation.md)。

## 统一入口

- [GitHub main](https://github.com/Cheng-xiu/ustc-danmaku/tree/main) 是完整源码及现行文档的统一入口；本轮集成通过 [PR #2](https://github.com/Cheng-xiu/ustc-danmaku/pull/2)，实际提交和合并状态以该 PR 为准。
- 根 `README.md`、[文档索引](README.md)、[网页指南](web-demo-guide.md)、[规则](demo-rules.md)、[ABI](web-abi.md)、[实施规划](web-demo-plan.md) 和[外部代理提示词](web-demo-agent-prompt.md) 共同描述当前产品；未来训练仍是规划。
- `demo/ustc-danmaku.html` 是可双击的单文件游戏；[GitHub Pages](https://cheng-xiu.github.io/ustc-danmaku/) 发布相同游戏字节。线上来源提交及哈希以公开 [deployment.json](https://cheng-xiu.github.io/ustc-danmaku/deployment.json) 为准。
- 原规范、Windows 原型、各版本报告和原始验收数据在[文档索引的历史分区](README.md)中注明身份；其中旧路径按[映射表](monorepo.md#旧路径映射与历史证据)解释，不将历史数值当成现行规则。

## 本轮同步内容

补齐原文档索引和机器学习规划中残留的 cfg5/ABI4/15源文件描述，使用当前 cfg6/ABI5/16源文件及实际 monorepo 路径；实施任务复用已存在的桥接、解码和性能入口，不要求重复从零建模块。

源码仓库的默认服务器和启动器只读取 `apps/web/dist`；当前输出未构建时提示先构建，避免旧克隆残留的 `web/dist` 被自动打开。下载包保持 `web/dist` 布局。旧 cfg4 `endless_native_replay` 只作为显式开启的历史构建夹具，当前无尽验证使用 `test:endless` 的真实浏览器脚本。

试玩包包含当前文档、参考资料和协作约定，修复部署、规划及原规范链接缺失。根 `package:source` 导出源码快照并记录哈希；已有原生二进制仍如实标注来源未经打包过程核验。

## 复验与游戏身份

本轮根 `npm test` 已退出0：完成 Wasm/Web/单文件构建、CTest13/13、输入54例/431断言、Native/Wasm36,010快照、离线浏览器282检查与无尽回放18检查。另根 `npm run test:monorepo` 的73/73检查通过，包含7项旧构建残留隔离检查及四种真实HTTP启动；资源/脚本/HTTP错误均为0，全部子进程已结束。本次试玩包172项manifest文件核对一致，12项生产依赖许可证完整。日志与原始结果见 [本轮证据](validation/repository-sync/)。

原生sim/desktop同步当前帮助文案：sim动态显示 `demo-config-v6`，无效配置仍退出2；desktop明确为兼容调试入口、使用当前无尽规则和费用，完整预瞄/GPA展示以Web为准。最后增量原生构建及CTest13/13、MSVC的desktop/sim编译链接通过，没有打开图形窗口。

GitHub主线已合并PR #2，旧规划PR #1已关闭；本地主要工作树与实现分支同步到主线。Pages在线复验在1440×900、1024×768、390×844三种窗口中55/55通过，页面脚本、资源请求及HTTP错误均为0。公开HTML与仓库游戏文件逐字节一致，来源提交由 `deployment.json` 记录。详见[在线浏览器验收](validation/repository-sync/pages-browser-smoke.json)。

在线测试原先连续执行移动和发射，移动后的出弹点恰好进入学生路径，24发弹幕在首tick碰撞后全部消失，导致存活弹幕的轨迹断言误报。现通过真实R重开隔离两组操作，保留原弹速、半径及首tick位置断言和容差；每种窗口均找到与锁定预瞄线一致的实弹。该修改仅涉及测试，公共快照和碰撞诊断保存在[诊断记录](validation/repository-sync/pages-diagnostic.json)。

原本地Windows工作树的15个未提交文件已在仓库外逐文件备份并验证哈希，切换主线时另以Git stash保留；恢复信息记录在本地执行结果中。当前C核心、Wasm桥接与网页游戏源码没有为文档/工具同步新增版本，费用仍为25/50/20/100，手动瞄准、无尽波次和 GPA 规则以当前 C 配置及规则文档为准。

游戏 HTML 仍为 1,168,141 bytes，SHA256 `9de27925103b644236b736e635932be215ecd0440d095311657c08673d22680d`；其原始 v6 交付身份及游戏行为证据见[瞄准登记](validation/aim-v6.md)。本轮同步记录与先前版本记录分别保存。
