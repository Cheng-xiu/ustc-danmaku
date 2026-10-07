# 网页无尽 Demo 运行指南

当前配置 v5、Web ABI v4。[在线试玩](https://cheng-xiu.github.io/ustc-danmaku/)，或双击 `demo/ustc-danmaku.html` 离线打开；所有资源和许可证均在单文件内。

## 操作与界面

顶部显示生命、GPA、波次、击倒、明显的能量条和共享出招 CD；余下窗口为战场。四招说明放在开始菜单，开始后隐藏。鼠标移到顶部栏仍朝指针方向移动；移出浏览器可观察区域保留最后位置，失焦自动暂停。有 WASD/方向键输入时优先键盘，数字 1–4 或顶栏按钮出招，Esc/战场右键暂停，R 重开。

每局开始按战场窗口比例取等面积场地，保持面积 960×720。用户选择中途改变窗口比例时本局等比显示，重开才按新比例铺满；圆形校徽和已公开弹道不变。极小窗口重开若不满足合法场地，提示调整窗口并保留旧暂停局。

| 键 | 用途 | 能量 | 预警 + 攻击期 |
| --- | --- | --- | --- |
| 1 | 近中距快速双环 | 25 | 0.4 + 1.5 秒 |
| 2 | 三列封两留一的分列弹墙 | 50 | 1 + 4 秒 |
| 3 | 锁定单目标的窄扇三连 | 20 | 0.3 + 1.25 秒 |
| 4 | 全场持续扫描雨 | 100 | 1.5 + 5.5 秒 |

共享能量初始 60、上限 100、每逻辑秒恢复 10；接受才扣费，拒绝不扣、不排队。CD 是当前招式占用的剩余时间，IDLE 才可再出招；没有四套独立冷却。金矿安全原点与全部存活学生距离不足120时拒绝，可换位置或换招。

初始 3 名学生，清波后 120 tick（2 秒）出生预告并增加 1 名，最多8名后持续刷新；只有Boss死亡结束。GPA仅由累计真实击倒n计算 `floor(430*n/(n+20))/100`，20人为2.15，100人为3.58。暂停/失焦/页面隐藏/死亡停止逻辑，重开清零。

## 构建

需要 Node 22.12+、npm、固定 Emscripten 6.0.11。从仓库根执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup-web-toolchain.ps1
powershell -ExecutionPolicy Bypass -File scripts/build-wasm.ps1
Set-Location web
npm.cmd ci
npm.cmd run build
Set-Location ..
node scripts/build-standalone.mjs demo/ustc-danmaku.html
```

修改 C 后必须先重建 Wasm。原生与 Wasm 共用根 CMake 的15个C/AI源文件，TypeScript只做显示与输入。原始web/index.html需Vite/HTTP；可双击的交付文件是生成后的单文件。

开发时在web目录运行npm.cmd run dev，生产预览用npm.cmd run preview -- --port 4173。最终自包含HTML无需服务器，发布方式见[Pages指南](github-pages.md)。

## 验证与复跑

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-native.ps1
./build/native/web_bridge_probe.exe
node tests/web/compare-core.mjs
node tests/web/fullscreen-smoke.mjs demo/ustc-danmaku.html
node tests/web/fullscreen-endless-smoke.mjs demo/ustc-danmaku.html
```

浏览器脚本需要本机Chrome，运行后在finally关闭自建实例。fullscreen-smoke覆盖三种窗口、实际鼠标/键盘、四招费用/完整CD/公开生成时刻、提示布局、缩放与重开；endless-smoke用合法真实键盘和当前公开状态完成清波、120tick预告、下一波及重开。QA只读，不改核心状态。受控时钟用于确定性验收，不作为FPS测量。

当前证据位于[validation/endless-v5](validation/endless-v5/)，[平衡报告](web-balance-findings-v5.md)给出原生对照复跑参数。[v4工程报告](archive/web-demo-validation-v4-20261007.md)和旧有限局资料保留。旧browser-smoke/standalone-smoke/endless-browser-replay含旧布局或旧时序前提，不能直接用于当前产品验收。

可选打包：`powershell -ExecutionPolicy Bypass -File scripts/package-web.ps1 -OutputRoot build/release-v5`。输出目录和ZIP存在时明确拒绝覆盖。加载错误时重新完整构建并使用支持WebAssembly/WebGL的浏览器，不放宽ABI校验。
