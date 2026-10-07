# 网页核心工具链与桥接

当前源码配置v5、ABI v4，构建与验收见[运行指南](web-demo-guide.md)和[工程验证](web-demo-validation.md)。下方早期发数/解码条数是历史记录，不代表现版参数；默认960×720四招公开计划分别48/48/27/80发，消耗25/50/20/100，淋浴在自然蓄能后接受。本轮核心构建和21,606快照原生/Wasm对照已通过；全屏新版实际浏览器验收仍在进行，不把构建结果写成发布通过。

固定 Emscripten **6.0.11**；来源为官方 [emsdk](https://github.com/emscripten-core/emsdk)，安装时官方 `emsdk list` 推荐此具体稳定版本，release 映射为 `f6264d4a4dd9ba24a9f0a5702835a44d1463de13`。2026-10-07 已实际安装并激活到 `C:/Users/jhsly/.dsh/toolchains/emsdk`；默认目录由当前用户目录计算，不需要管理员权限，不进行系统级安装。该版 Windows 编译器入口是 `emcc.exe`。

在仓库根目录使用 Windows PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup-web-toolchain.ps1
powershell -ExecutionPolicy Bypass -File scripts/build-wasm.ps1
```

已安装同版本时可直接构建。自定义目录使用两脚本的 `-SdkRoot` 参数；脚本只在当前进程导入环境。安装脚本对既有目录核对官方 Git remote，再执行具体版本的 `install` 与 `activate`。不使用随时间变化的 `latest`。

构建输出为 `web/public/wasm/demo-core.mjs` 与 `demo-core.wasm`。前端包管理和启动命令见网页运行指南。源码模块入口通过 HTTP 访问；scripts/build-standalone.mjs 将同一模块、Wasm和资产内嵌成单文件，预构建 HTML 可直接通过 file:// 离线打开。加载器依据 Vite `BASE_URL` 定位模块，并把 Wasm 文件定位到模块同目录；Emscripten 输出为 ES 模块工厂，可用于浏览器和 Node。这里没有加入线程、SharedArrayBuffer、跨源隔离或文件系统依赖。

`wasm/CMakeLists.txt` 与 PowerShell 构建脚本均读取根 `CMakeLists.txt` 冻结的 **15** 个 `core/ai` 源文件，包含本轮新增的 `core/field_config.c`，桥接为 `wasm/demo_bridge.c`。不引入 EasyX、Windows 输入或 C++ 渲染源文件。CMake 非 Emscripten 构建生成 `demo_web_bridge` 静态库，原生回放可链接同一桥接；原生直接编译时加入相同核心清单、`-Icore -Iai -Iwasm` 和数学库。

公共接口和布局以 [Web ABI v4](web-abi.md) 为准，新增 `demo_reset_sized`，旧 `demo_reset` 保留默认场地入口。`demo_config_set_field_size` 在新局一次配置尺寸和合法出生/边界，速度、半径、费用与时长固定；原生/Wasm复用同一C实现，非法尺寸不覆盖进行中的合法世界。C 使用固定单实例世界，显式写入每个小端 word；浮点值以 `memcpy` 转换位模式，不 memcpy 核心结构体。预警调用接受计划的真实 emit，按计划 ID 缓存公开出生几何，不调用 RNG，也不输出 `geometry_seed`。核心与桥接单 tick 前进；前端每 tick 立即取快照并汇总事件。快照指针仅供当前调用，任何容量失败返回空指针及零长度，客户端抛出明确错误。

网页新局按顶栏下的战场视口比例计算尺寸，保持逻辑面积960×720。用户已批准进行中改变比例只等比缩放、允许留边，重开才按新比例铺满；不能在resize时改写现有C世界、已接受计划或将圆形贴图拉伸成椭圆。不同尺寸淋浴发数依据同一C约束保持间距，不能把默认80发当成所有窗口比例的恒定总发数。

TypeScript 先复制 Wasm 内存再严格检查 magic、版本、长度、偏移、容量和记录值。64 位种子、计划与弹幕身份转换为十进制字符串，避免 Number 精度丢失。每次取快照重新获取 `HEAPU8`，不持有 memory.grow 前的旧视图。`dispose()` 释放的是游戏单实例状态；模块内存由 JavaScript 垃圾回收器在客户端失去引用后回收。

v5最终网页实际验收使用 `tests/web/fullscreen-smoke.mjs`；旧三个浏览器/多波回放夹具含旧固定场地、时长或鼠标语义，仅保留历史，不将其旧通过数自动继承。当前compare-core的6场景、21,606快照包含3种非默认尺寸；最终单文件与线上页面须另做本版实际浏览器检查。`scripts/package-web.ps1`只构建/复制/记录manifest，不代替上述验收，打包须在母代理完成最终检查后进行。

本轮未使用 DeepSeek 模型；文件由当前 Codex 执行器实现，规划中的外部代理路由仍保留为外部执行说明。

## 2026-10-07 工具链历史检查（有限局迁移阶段）

- PowerShell 构建成功，`emcc --version` 报 6.0.11（编译器修订 `a0014542110d6078c3a1a7941fa1ddb3a2281f16`），优化选项为 `-O2`。
- 原生 GCC 8.1.0 / C11 构建桥接成功；独立 `wasm/CMakeLists.txt` 原生配置、静态库及 `demo_bridge_probe` 构建成功。探针核对完整 64 位种子、8 学生、重置/释放及延迟到 tick 120 接受四招，逐波预警数与真实生成事件一致：环 42 发/3 波、课表 72/3、金矿 36/1、淋浴 80/5。
- Wasm ES 模块可由 Node 初始化。真实快照经 TypeScript 解码累计核对 931 个后续 tick，四招上述总发数一致；旧解码结果在后续 step/reset 后仍保持原值；非法 magic 被拒绝。
- `client.ts` 通过 TypeScript 严格类型检查。这里只确认核心工具链和桥接；完整网页交互、逐 tick 跨平台回放和显示性能由独立集成验收记录说明，不据此承诺浏览器性能。

原生探针也可以用 CMake 在独立目录构建，不与其他代理共享缓存：

```powershell
cmake -S wasm -B build/bridge-native -DCMAKE_BUILD_TYPE=Release
cmake --build build/bridge-native --config Release
ctest --test-dir build/bridge-native -C Release --output-on-failure
```

若使用 MinGW，配置时显式传入 `-G "MinGW Makefiles"`、`-DCMAKE_C_COMPILER=<gcc.exe>`、`-DCMAKE_MAKE_PROGRAM=<mingw32-make.exe>`。若使用 Emscripten CMake 路径，先导入 SDK 环境，再传其 `upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake` 为 `CMAKE_TOOLCHAIN_FILE`；浏览器交付默认使用已验证的 PowerShell 构建入口。
