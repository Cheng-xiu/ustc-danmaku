# 网页核心工具链与桥接

当前源码为 monorepo、配置 v6、ABI v5，构建入口见 [运行指南](web-demo-guide.md) 和 [目录约定](monorepo.md)。目录迁移后的实际构建、对照和浏览器结果见 [迁移复验](monorepo-validation.md)。当前玩法的既有验证见 [v6 工程记录](web-demo-validation.md)；下方有限局发数和解码条数保留历史身份，不能写成当前手动几何或迁移验收结果。

固定 Emscripten **6.0.11**；来源为官方 [emsdk](https://github.com/emscripten-core/emsdk)，安装时官方 `emsdk list` 推荐此具体稳定版本，release 映射为 `f6264d4a4dd9ba24a9f0a5702835a44d1463de13`。2026-10-07 已实际安装并激活到 `C:/Users/jhsly/.dsh/toolchains/emsdk`；默认目录由当前用户目录计算，不需要管理员权限，不进行系统级安装。该版 Windows 编译器入口是 `emcc.exe`。

在仓库根目录使用 Windows PowerShell：

```powershell
npm.cmd ci
npm.cmd run setup:wasm
npm.cmd run build
```

已安装同版本时可直接构建。自定义目录使用两脚本的 `-SdkRoot` 参数；脚本只在当前进程导入环境。安装脚本对既有目录核对官方 Git remote，再执行具体版本的 `install` 与 `activate`。不使用随时间变化的 `latest`。

`npm run build:wasm` 输出为 `apps/web/public/wasm/demo-core.mjs` 与 `demo-core.wasm`。`npm run build` 先生成 Wasm，再构建前端和单文件；`apps/web/dist/` 为 HTTP 静态版，`build/release/ustc-danmaku-endless.html` 为离线交付。源码模块入口通过 HTTP 访问；`scripts/build-standalone.mjs` 将同一模块、Wasm 和资产内嵌成单文件，可直接通过 file:// 打开。加载器依据 Vite `BASE_URL` 定位模块，并把 Wasm 文件定位到模块同目录；Emscripten 输出为 ES 模块工厂，可用于浏览器和 Node。这里没有加入线程、SharedArrayBuffer、跨源隔离或文件系统依赖。

`packages/core/sources.txt` 是唯一的 **16** 个共享 C/AI 源文件清单，路径相对 `packages/core/`，包含 `core/field_config.c` 与 `core/pattern_aim.c`。根 CMake、`packages/wasm/CMakeLists.txt` 与 PowerShell 构建/打包脚本经同一核心包读取它，桥接为 `packages/wasm/demo_bridge.c`。不引入 EasyX、Windows 输入或 C++ 渲染源文件。CMake 非 Emscripten 构建生成 `demo_web_bridge` 静态库，原生回放链接同一桥接；独立直接编译须使用相同清单、`-Ipackages/core/core -Ipackages/core/ai -Ipackages/wasm` 和数学库。

公共接口和布局以 [Web ABI v5](web-abi.md) 为准，保留 `demo_reset_sized` 和默认场地入口 `demo_reset`，提供 `demo_step_aim` 及独立 `demo_preview`/`demo_preview_size` 缓冲区。`demo_config_set_field_size` 在新局一次配置尺寸和合法出生/边界，速度、半径、费用与时长固定；原生/Wasm 复用同一 C 实现，非法尺寸不覆盖进行中的合法世界。C 使用固定单实例世界，显式写入每个小端 word；浮点值以 `memcpy` 转换位模式，不 memcpy 核心结构体。预警调用接受计划的真实 emit，按计划 ID 缓存公开出生几何；预瞄复用同一 C 工厂/emitter，不改世界、RNG、计划 ID、事件或已接受预警。核心与桥接单 tick 前进；前端每 tick 立即取快照并汇总事件。快照指针仅供当前调用，任何容量失败返回空指针及零长度，客户端抛出明确错误。

网页新局按顶栏下的战场视口比例计算尺寸，保持逻辑面积 960×720。用户已批准进行中改变比例只等比缩放、允许留边，重开才按新比例铺满；不能在 resize 时改写现有 C 世界、已接受计划或将圆形贴图拉伸成椭圆。v6 淋浴弹数随场地和方向投影按同一 C 约束确定，不能把旧默认 80 发当成恒定总发数。按住后的瞄准方向仅真实鼠标移动更新，Boss 移动、resize 和松手不重新转向。

TypeScript 先复制 Wasm 内存再严格检查 magic、版本、长度、偏移、容量和记录值。64 位种子、计划与弹幕身份转换为十进制字符串，避免 Number 精度丢失。每次取快照重新获取 `HEAPU8`，不持有 memory.grow 前的旧视图。`dispose()` 释放的是游戏单实例状态；模块内存由 JavaScript 垃圾回收器在客户端失去引用后回收。

根 `npm test` 顺序构建并执行原生、输入、Native/Wasm、`tests/web/aim-browser-smoke.mjs` 与 `aim-endless-smoke.mjs`，最终浏览器默认检查本次生成的单文件。既有 v6 对照为 10 场景、36,010 快照；旧 v5 和更早浏览器夹具含旧参数或鼠标语义，仅保留历史。迁移需重新执行当前检查，结果不能仅继承旧通过数。`scripts/package-web.ps1` 复制静态产物、生成单文件并记录 manifest，不代替玩法验收；最终单文件与线上页面各自检查。

本轮未使用 DeepSeek 模型；文件由当前 Codex 执行器实现，规划中的外部代理路由仍保留为外部执行说明。

## 2026-10-07 工具链历史检查（有限局迁移阶段）

本节保留当时的旧路径与结论；对应现版目录见 [路径映射](monorepo.md#旧路径映射与历史证据)。

- PowerShell 构建成功，`emcc --version` 报 6.0.11（编译器修订 `a0014542110d6078c3a1a7941fa1ddb3a2281f16`），优化选项为 `-O2`。
- 原生 GCC 8.1.0 / C11 构建桥接成功；独立 `wasm/CMakeLists.txt` 原生配置、静态库及 `demo_bridge_probe` 构建成功。探针核对完整 64 位种子、8 学生、重置/释放及延迟到 tick 120 接受四招，逐波预警数与真实生成事件一致：环 42 发/3 波、课表 72/3、金矿 36/1、淋浴 80/5。
- Wasm ES 模块可由 Node 初始化。真实快照经 TypeScript 解码累计核对 931 个后续 tick，四招上述总发数一致；旧解码结果在后续 step/reset 后仍保持原值；非法 magic 被拒绝。
- `client.ts` 通过 TypeScript 严格类型检查。这里只确认核心工具链和桥接；完整网页交互、逐 tick 跨平台回放和显示性能由独立集成验收记录说明，不据此承诺浏览器性能。

原生探针也可以用 CMake 在独立目录构建，不与其他代理共享缓存：

```powershell
cmake -S packages/wasm -B build/bridge-native -DCMAKE_BUILD_TYPE=Release
cmake --build build/bridge-native --config Release
ctest --test-dir build/bridge-native -C Release --output-on-failure
```

若使用 MinGW，配置时显式传入 `-G "MinGW Makefiles"`、`-DCMAKE_C_COMPILER=<gcc.exe>`、`-DCMAKE_MAKE_PROGRAM=<mingw32-make.exe>`。若使用 Emscripten CMake 路径，先导入 SDK 环境，再传其 `upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake` 为 `CMAKE_TOOLCHAIN_FILE`；浏览器交付默认使用已验证的 PowerShell 构建入口。
