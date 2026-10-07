# 网页无尽 Demo 运行指南

当前配置 v6、Web ABI v5。[在线试玩](https://cheng-xiu.github.io/ustc-danmaku/)，或双击 `demo/ustc-danmaku.html` 离线打开；所有资源和许可证均在单文件内。

## 操作与界面

顶部显示生命、GPA、波次、击倒、明显的能量条和共享出招 CD；余下窗口为战场。四招说明放在开始菜单，开始后隐藏。鼠标移到顶部栏仍朝指针方向移动；移出浏览器可观察区域保留最后位置，失焦自动暂停。有 WASD/方向键输入时优先键盘，按住数字 1–4 或顶栏技能按钮预瞄，松开释放，Space 取消，Esc/战场右键暂停，R 重开。

每局开始按战场窗口比例取等面积场地，保持面积 960×720。用户选择中途改变窗口比例时本局等比显示，重开才按新比例铺满；圆形校徽和已公开弹道不变。极小窗口重开若不满足合法场地，提示调整窗口并保留旧暂停局。

| 键 | 用途 | 能量 | 预警 + 攻击期 |
| --- | --- | --- | --- |
| 1 | 近中距前向宽弧双波 | 25 | 0.4 + 1.5 秒 |
| 2 | 三列封两留一的分列弹墙 | 50 | 1 + 4 秒 |
| 3 | 朝鼠标方向的窄扇三连 | 20 | 0.3 + 1.25 秒 |
| 4 | 全场持续扫描雨 | 100 | 1.5 + 5.5 秒 |

共享能量初始 60、上限 100、每逻辑秒恢复 10；接受才扣费，拒绝不扣、不排队。CD 是当前招式占用的剩余时间，IDLE 才可再出招；没有四套独立冷却。金矿 Boss 原点与全部存活学生距离不足120时拒绝，可换位置或换招。

初始 3 名学生，清波后 120 tick（2 秒）出生预告并增加 1 名，最多8名后持续刷新；只有Boss死亡结束。GPA仅由累计真实击倒n计算 `floor(430*n/(n+20))/100`，20人为2.15，100人为3.58。暂停/失焦/页面隐藏/死亡停止逻辑，重开清零。

按住期间游戏继续运行，不扣费、不启动 CD。新一次预瞄从当前 Boss 指向鼠标；持有后角色走动只平移预瞄原点，鼠标实际移动才改变方向，松手不重新计算。鼠标距 Boss 不足 12 个逻辑像素时保留最后有效方向，避免近处抖动或反转。没有鼠标朝向时使用最近键盘朝向，默认向上。方向预瞄只画第一波的大概轨迹，并不保证整条线都会命中；松手后 C 锁定完整计划。

四招全向鼠标方向：1 为 160° 前向宽弧，2 为可转向分列墙，3 为 40° 窄扇三连，4 为可转向扫描雨。墙和雨从场地后缘生成，箭头表示整体行进方向。能量/CD 不足仍可预瞄，松手时检查；拒绝不排队，条件恢复不会自动补发。取消、暂停、失焦、重开、死亡会清除待释放。

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

修改 C 后必须先重建 Wasm。原生与 Wasm 共用根 CMake 的16个C/AI源文件，TypeScript只做显示与输入。原始web/index.html需Vite/HTTP；可双击的交付文件是生成后的单文件。

开发时在web目录运行npm.cmd run dev，生产预览用npm.cmd run preview -- --port 4173。最终自包含HTML无需服务器，发布方式见[Pages指南](github-pages.md)。

## 验证与复跑

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-native.ps1
./build/native/web_bridge_probe.exe
node tests/web/compare-core.mjs
node tests/web/aim-input-probe.mjs
node tests/web/aim-browser-smoke.mjs demo/ustc-danmaku.html
node tests/web/aim-endless-smoke.mjs demo/ustc-danmaku.html
```

浏览器脚本需要本机 Chrome，使用真实鼠标/键盘与受控时钟；QA 只读。aim-browser-smoke 检查三种窗口、按住/松手/取消、四招 C 预瞄与接受后弹道、真实首发弹、HUD 拖出释放、方向稳定、新一次按住对准当前鼠标、缩放和生命周期。aim-endless-smoke 验证当前手动操作的清波和下一波出生。受控时钟用于逻辑验收，不作为 FPS 测量。

当前证据见 [v6 工程验证](web-demo-validation.md) 和 [瞄准规则与玩法建议](validation/aim-v6.md)。此前 [v5 平衡报告](web-balance-findings-v5.md) 记录旧自动方向弹形，不能替代 v6 手动瞄准的真人平衡验收；历史脚本与报告独立保留。

可选打包：`powershell -ExecutionPolicy Bypass -File scripts/package-web.ps1 -OutputRoot build/release-v6`。输出目录和ZIP存在时明确拒绝覆盖。加载错误时重新完整构建并使用支持WebAssembly/WebGL的浏览器，不放宽ABI校验。
